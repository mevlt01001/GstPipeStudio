// elkengine.cpp – see elkengine.h.
#include "elkengine.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "quickjs.h"

namespace layout {
namespace {

// A port is a point: ELK then starts and ends a link exactly on the pad centre the canvas draws.
constexpr double kPortSize = 0;

// elkjs expects a browser or Node. QuickJS has neither: give it a global object, a silent console and a setTimeout that
// only queues, and run the queue ourselves until the layout promise settles.
const char* kShim = R"js(
var global = globalThis;
globalThis.console = { log: function () {}, warn: function () {}, error: function () {} };
globalThis.__timers = [];
globalThis.setTimeout = function (f) { __timers.push(f); return __timers.length; };
)js";
const char* kGlue = R"js(
var __elk = new ELK();
function __layout(text) {
    __result = null; __error = null;
    __elk.layout(JSON.parse(text)).then(function (r) { __result = JSON.stringify(r); },
                                        function (e) { __error = String(e && e.message || e); });
}
function __pump() { var t = __timers; __timers = []; for (var i = 0; i < t.length; ++i) t[i](); return t.length; }
)js";

QJsonObject options(const Node& n, bool root, Routing routing) {
    QJsonObject o;
    if (n.frame()) {
        o["org.eclipse.elk.algorithm"] = "layered";
        o["org.eclipse.elk.direction"] = "RIGHT";
        o["org.eclipse.elk.edgeRouting"] = routing == Routing::Curves ? "SPLINES" : "ORTHOGONAL";
        o["org.eclipse.elk.layered.crossingMinimization.strategy"] = "LAYER_SWEEP";
        o["org.eclipse.elk.layered.thoroughness"] = "20";
        o["org.eclipse.elk.layered.spacing.nodeNodeBetweenLayers"] = "70";
        o["org.eclipse.elk.spacing.nodeNode"] = "40";
        o["org.eclipse.elk.spacing.edgeNode"] = "20";
        o["org.eclipse.elk.layered.nodePlacement.strategy"] = "BRANDES_KOEPF";
        if (!root) {
            o["org.eclipse.elk.portConstraints"] = "FIXED_SIDE";
            o["org.eclipse.elk.padding"] = "[top=40,left=24,bottom=24,right=24]";
        }
    } else {
        o["org.eclipse.elk.portConstraints"] = "FIXED_POS";
    }
    return o;
}

QJsonObject toElk(const Node& n, bool root, Routing routing) {
    QJsonObject o;
    o["id"] = n.id;
    o["layoutOptions"] = options(n, root, routing);
    if (!n.frame()) {
        o["width"] = n.width;
        o["height"] = n.height;
    }
    QJsonArray ports;
    int west = 0, east = 0;
    for (const Port& p : n.ports) (p.east ? east : west)++;
    int westAt = 0, eastAt = 0;
    for (const Port& p : n.ports) {
        QJsonObject port{{"id", p.id}, {"width", kPortSize}, {"height", kPortSize}};
        QJsonObject po{{"org.eclipse.elk.port.side", p.east ? "EAST" : "WEST"},
                       {"org.eclipse.elk.port.borderOffset", QString::number(-kPortSize / 2)},  // centred on the edge line
                       // ELK numbers ports clockwise from the top left: east ones top down, west ones bottom up.
                       {"org.eclipse.elk.port.index", QString::number(p.east ? eastAt++ : west - 1 - westAt++)}};
        port["layoutOptions"] = po;
        if (!n.frame()) {
            port["x"] = p.east ? n.width - kPortSize / 2 : -kPortSize / 2;
            port["y"] = p.y - kPortSize / 2;
        }
        ports.append(port);
    }
    o["ports"] = ports;
    QJsonArray children, edges;
    for (const Node& c : n.children) children.append(toElk(c, false, routing));
    int piece = 0;
    for (const Edge& e : n.edges) {
        edges.append(QJsonObject{{"id", e.id + "~" + n.id + "~" + QString::number(piece++)}, {"sources", QJsonArray{e.from}},
                                 {"targets", QJsonArray{e.to}}});
    }
    o["children"] = children;
    o["edges"] = edges;
    return o;
}

QList<QPointF> sampled(const QJsonObject& section, QPointF offset, Routing routing) {
    QList<QPointF> points;
    auto at = [&](const QJsonValue& v) { return QPointF(v["x"].toDouble(), v["y"].toDouble()) + offset; };
    points << at(section["startPoint"]);
    for (const QJsonValue& b : section["bendPoints"].toArray()) points << at(b);
    points << at(section["endPoint"]);
    // Curves come as cubic Bézier control points: start, then (c1, c2, end) per piece.
    if (routing != Routing::Curves || (points.size() - 1) % 3 != 0) return points;
    QList<QPointF> out{points.front()};
    for (int k = 0; k + 3 < points.size(); k += 3) {
        for (int t = 1; t <= 12; ++t) {
            const double u = t / 12.0, a = (1 - u) * (1 - u) * (1 - u), b = 3 * u * (1 - u) * (1 - u), c = 3 * u * u * (1 - u), d = u * u * u;
            out << a * points[k] + b * points[k + 1] + c * points[k + 2] + d * points[k + 3];
        }
    }
    return out;
}

void read(const QJsonObject& n, QPointF parent, Routing routing, Result* out) {
    const QPointF at = parent + QPointF(n["x"].toDouble(), n["y"].toDouble());
    out->nodes[n["id"].toString()] = QRectF(at, QSizeF(n["width"].toDouble(), n["height"].toDouble()));
    for (const QJsonValue& p : n["ports"].toArray())
        out->ports[p["id"].toString()] = at + QPointF(p["x"].toDouble() + kPortSize / 2, p["y"].toDouble() + kPortSize / 2);
    for (const QJsonValue& e : n["edges"].toArray()) {
        const QString link = e["id"].toString().section('~', 0, 0);
        for (const QJsonValue& s : e["sections"].toArray()) out->edges[link] << sampled(s.toObject(), at, routing);
    }
    for (const QJsonValue& c : n["children"].toArray()) read(c.toObject(), at, routing, out);
}

}  // namespace

struct ElkEngine::Js {
    JSRuntime* runtime = nullptr;
    JSContext* context = nullptr;
    QString loadError;

    bool eval(const QByteArray& code, const char* file, QString* error) {
        JSValue v = JS_Eval(context, code.constData(), static_cast<size_t>(code.size()), file, JS_EVAL_TYPE_GLOBAL);
        const bool ok = !JS_IsException(v);
        JS_FreeValue(context, v);
        if (!ok && error) {
            JSValue e = JS_GetException(context);
            const char* text = JS_ToCString(context, e);
            *error = QString("%1: %2").arg(file, text ? text : "unknown error");
            JS_FreeCString(context, text);
            JS_FreeValue(context, e);
        }
        return ok;
    }
    QString global(const char* name) {
        JSValue g = JS_GetGlobalObject(context), v = JS_GetPropertyStr(context, g, name);
        QString text;
        if (JS_IsString(v)) {
            const char* s = JS_ToCString(context, v);
            text = QString::fromUtf8(s);
            JS_FreeCString(context, s);
        }
        JS_FreeValue(context, v);
        JS_FreeValue(context, g);
        return text;
    }
};

ElkEngine::ElkEngine() : js_(std::make_unique<Js>()) {}

ElkEngine::~ElkEngine() {
    if (js_->context) JS_FreeContext(js_->context);
    if (js_->runtime) JS_FreeRuntime(js_->runtime);
}

bool ElkEngine::run(const Node& root, Routing routing, Result* out, QString* error) {
    if (!js_->runtime) {
        js_->runtime = JS_NewRuntime();
        JS_SetMaxStackSize(js_->runtime, 8 * 1024 * 1024);  // ELK recurses deeply; the layout thread has the room
        js_->context = JS_NewContext(js_->runtime);
        QFile bundle(":/layout/elk.bundled.js");
        if (!bundle.open(QIODevice::ReadOnly)) js_->loadError = "the ELK bundle is missing from the resources";
        else if (!js_->eval(kShim, "shim.js", &js_->loadError) || !js_->eval(bundle.readAll(), "elk.bundled.js", &js_->loadError) ||
                 !js_->eval(kGlue, "glue.js", &js_->loadError)) {
        }
    }
    if (!js_->loadError.isEmpty()) {
        if (error) *error = js_->loadError;
        return false;
    }
    const QByteArray input = QJsonDocument(toElk(root, true, routing)).toJson(QJsonDocument::Compact);
    QByteArray call = "__layout(" + QJsonDocument(QJsonArray{QString::fromUtf8(input)}).toJson(QJsonDocument::Compact).mid(1);
    call.chop(1);
    call += ")";
    if (!js_->eval(call, "layout.js", error)) return false;
    for (int round = 0; round < 100000; ++round) {
        JSContext* job = nullptr;
        bool worked = false;
        while (JS_ExecutePendingJob(js_->runtime, &job) > 0) worked = true;
        JSValue g = JS_GetGlobalObject(js_->context), pump = JS_GetPropertyStr(js_->context, g, "__pump");
        JSValue ran = JS_Call(js_->context, pump, g, 0, nullptr);
        int32_t timers = 0;
        JS_ToInt32(js_->context, &timers, ran);
        JS_FreeValue(js_->context, ran);
        JS_FreeValue(js_->context, pump);
        JS_FreeValue(js_->context, g);
        if (!worked && timers == 0) break;
    }
    const QString failure = js_->global("__error"), answer = js_->global("__result");
    if (!failure.isEmpty() || answer.isEmpty()) {
        if (error) *error = failure.isEmpty() ? QString("ELK gave no layout") : failure;
        return false;
    }
    *out = Result();
    read(QJsonDocument::fromJson(answer.toUtf8()).object(), QPointF(), routing, out);
    return true;
}

}  // namespace layout
