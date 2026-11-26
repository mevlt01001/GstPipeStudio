// layout_test.cpp – the ELK engine on its own: boxes keep their pads where they draw them, frames hold their children
// and carry ports on their border, links do not cross, and the runner delivers only the newest answer.
#include <QElapsedTimer>
#include <QLineF>
#include <QSignalSpy>
#include <QTest>

#include "elkengine.h"
#include "layoutrunner.h"

using namespace layout;

namespace {

Node box(const QString& id, QStringList sinks, QStringList srcs) {
    Node n;
    n.id = id;
    n.width = 240;
    n.height = 60 + 30 * qMax(sinks.size(), srcs.size());
    for (int i = 0; i < sinks.size(); ++i) n.ports.append({id + "|" + sinks[i] + "|sink", false, 65.0 + 30 * i});
    for (int i = 0; i < srcs.size(); ++i) n.ports.append({id + "|" + srcs[i] + "|src", true, 65.0 + 30 * i});
    return n;
}

// The DeepStream shape of the spike: cameras -> decode -> mux -> infer -> tee -> two branches. Decode of camera 0 is an
// expanded template: a frame with two ports and three boxes.
Node deepstream(int cameras) {
    Node root;
    root.id = "root";
    QStringList muxSinks;
    for (int i = 0; i < cameras; ++i) muxSinks << QString("sink_%1").arg(i);
    root.children << box("mux", muxSinks, {"src"}) << box("infer", {"sink"}, {"src"}) << box("tee", {"sink"}, {"src_0", "src_1"})
                  << box("osd", {"sink"}, {"src"}) << box("display", {"sink"}, {}) << box("enc", {"sink"}, {"src"}) << box("file", {"sink"}, {});
    auto link = [&](Node& in, const QString& id, const QString& from, const QString& to) { in.edges.append({id, from, to}); };
    for (int i = 0; i < cameras; ++i) {
        const QString cam = QString("cam%1").arg(i), dec = QString("dec%1").arg(i);
        root.children << box(cam, {}, {"src"});
        if (i == 0) {
            Node frame;
            frame.id = "Decode#1";
            frame.ports = {{"Decode#1|in", false, 0}, {"Decode#1|out", true, 0}};
            frame.children << box("depay", {"sink"}, {"src"}) << box("parse", {"sink"}, {"src"}) << box(dec, {"sink"}, {"src"});
            link(frame, "L-in", "Decode#1|in", "depay|sink|sink");
            link(frame, "L-a", "depay|src|src", "parse|sink|sink");
            link(frame, "L-b", "parse|src|src", dec + "|sink|sink");
            link(frame, "L-out", dec + "|src|src", "Decode#1|out");
            root.children << frame;
            link(root, "L-in", cam + "|src|src", "Decode#1|in");
            link(root, "L-out", "Decode#1|out", "mux|sink_0|sink");
        } else {
            root.children << box(dec, {"sink"}, {"src"});
            link(root, "L-cam" + QString::number(i), cam + "|src|src", dec + "|sink|sink");
            link(root, "L-dec" + QString::number(i), dec + "|src|src", QString("mux|sink_%1|sink").arg(i));
        }
    }
    link(root, "L1", "mux|src|src", "infer|sink|sink");
    link(root, "L2", "infer|src|src", "tee|sink|sink");
    link(root, "L3", "tee|src_0|src", "osd|sink|sink");
    link(root, "L4", "osd|src|src", "display|sink|sink");
    link(root, "L5", "tee|src_1|src", "enc|sink|sink");
    link(root, "L6", "enc|src|src", "file|sink|sink");
    return root;
}

bool cross(QPointF a, QPointF b, QPointF c, QPointF d) {
    auto side = [](QPointF p, QPointF q, QPointF r) { return (q.x() - p.x()) * (r.y() - p.y()) - (q.y() - p.y()) * (r.x() - p.x()); };
    const double d1 = side(c, d, a), d2 = side(c, d, b), d3 = side(a, b, c), d4 = side(a, b, d);
    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0)) && qAbs(d1) > 1e-6 && qAbs(d2) > 1e-6;
}

int crossings(const Result& r) {
    QList<QList<QPointF>> lines;
    for (const auto& pieces : r.edges) for (const auto& piece : pieces) lines << piece;
    int count = 0;
    for (int i = 0; i < lines.size(); ++i)
        for (int j = i + 1; j < lines.size(); ++j) {
            bool hit = false;
            for (int k = 1; k < lines[i].size() && !hit; ++k)
                for (int m = 1; m < lines[j].size() && !hit; ++m) hit = cross(lines[i][k - 1], lines[i][k], lines[j][m - 1], lines[j][m]);
            count += hit;
        }
    return count;
}

}  // namespace

class LayoutTest : public QObject {
    Q_OBJECT
private slots:
    void elkPlacesEverythingWithoutCrossings() {
        for (Routing routing : {Routing::Curves, Routing::RightAngles}) {
            ElkEngine engine;
            Result r;
            QString error;
            QElapsedTimer clock;
            clock.start();
            QVERIFY2(engine.run(deepstream(5), routing, &r, &error), qPrintable(error));
            const qint64 first = clock.restart();
            QVERIFY2(engine.run(deepstream(11), routing, &r, &error), qPrintable(error));  // loaded: layout time only
            qInfo() << "first run (loads ELK)" << first << "ms, next (11 cameras)" << clock.elapsed() << "ms" << (routing == Routing::Curves ? "curves" : "right angles");
            QVERIFY2(engine.run(deepstream(5), routing, &r, &error), qPrintable(error));
            for (const char* id : {"mux", "cam4", "dec4", "depay", "Decode#1", "file"}) QVERIFY2(r.nodes.contains(id), qPrintable(id));
            // A box's port is where the box draws its pad.
            const QRectF mux = r.nodes["mux"];
            QCOMPARE(r.ports["mux|sink_2|sink"], QPointF(mux.left(), mux.top() + 65 + 60));
            QCOMPARE(r.ports["mux|src|src"], QPointF(mux.right(), mux.top() + 65));
            // A frame holds its children and has its ports on its left and right borders.
            const QRectF frame = r.nodes["Decode#1"];
            for (const char* child : {"depay", "parse", "dec0"}) QVERIFY(frame.contains(r.nodes[child]));
            QCOMPARE(r.ports["Decode#1|in"].x(), frame.left());
            QCOMPARE(r.ports["Decode#1|out"].x(), frame.right());
            // Every link has a route; one through the frame has a piece outside and a piece inside.
            QCOMPARE(r.edges.size(), 18);  // 16 links, two of them with a piece on each side of the frame border
            QCOMPARE(r.edges["L-in"].size(), 2);
            QCOMPARE(crossings(r), 0);
            // Boxes do not overlap.
            for (auto a = r.nodes.cbegin(); a != r.nodes.cend(); ++a)
                for (auto b = r.nodes.cbegin(); b != r.nodes.cend(); ++b)
                    if (a.key() < b.key() && !a.value().contains(b.value()) && !b.value().contains(a.value()))
                        QVERIFY2(!a.value().intersects(b.value()), qPrintable(a.key() + " / " + b.key()));
        }
    }

    // The canvas draws a route only when it starts and ends on its two pads: both link styles must.
    void aRouteStartsAndEndsAtItsPads() {
        for (Routing routing : {Routing::Curves, Routing::RightAngles}) {
            ElkEngine engine;
            Result r;
            QString error;
            QVERIFY2(engine.run(deepstream(3), routing, &r, &error), qPrintable(error));
            const QList<QPointF> piece = r.edges["L3"].value(0);
            QVERIFY(piece.size() >= 2);
            qInfo() << int(routing) << piece.front() << r.ports["tee|src_0|src"] << piece.back() << r.ports["osd|sink|sink"];
            QVERIFY(QLineF(piece.front(), r.ports["tee|src_0|src"]).length() < 1.5);
            QVERIFY(QLineF(piece.back(), r.ports["osd|sink|sink"]).length() < 1.5);
        }
    }

    void runnerDeliversOnlyTheNewestAnswer() {
        Runner runner([] { return std::make_unique<ElkEngine>(); });
        QSignalSpy done(&runner, &Runner::finished);
        runner.request(deepstream(3), Routing::Curves);
        const quint64 last = runner.request(deepstream(4), Routing::Curves);
        QVERIFY(done.wait(20000));
        QTest::qWait(200);
        QCOMPARE(done.count(), 1);
        QCOMPARE(done[0][0].toULongLong(), last);
        QVERIFY(done[0][1].toBool());
        QVERIFY(done[0][2].value<Result>().nodes.contains("cam3"));
    }
};

QTEST_MAIN(LayoutTest)
#include "layout_test.moc"
