// controller_test.cpp – every flow of SPEC 8 driven through the Controller against the fake helpers.
#include <QDir>
#include <QLineF>
#include <QSignalSpy>
#include <QFileInfo>
#include <QTemporaryDir>

#include "outline.h"
#include "projectfile.h"
#include "templates.h"
#include "testsupport.h"
#include "versionhistory.h"

using namespace support;

namespace {

struct Chain {
    QString src, convert, sink;
};

QString text(const std::string& s) { return QString::fromStdString(s); }

}  // namespace

class ControllerTest : public QObject {
    Q_OBJECT

    std::unique_ptr<Controller> c;

    Chain makeChain() {
        Chain ch;
        ch.src = addSource(*c, "videotestsrc");
        ch.convert = addAfter(*c, ch.src, "src", "videoconvert");
        ch.sink = addAfter(*c, ch.convert, "src", "fakesink");
        return ch;
    }

private slots:
    void init() { c = makeController(); }
    void cleanup() { c.reset(); }

    void catalogAndTraits() {
        QVERIFY(c->services().catalog().loaded());
        auto find = [&](const QString& f) {
            for (const FactoryEntry& e : c->services().catalog().entries()) {
                if (fromStd(e.factory) == f) return e;
            }
            return FactoryEntry();
        };
        QVERIFY(find("videotestsrc").isSource);
        QVERIFY(!find("videotestsrc").nvidia);
        QVERIFY(find("nvvideoconvert").nvidia);
        QVERIFY(find("tee").hasRequestSrc);
        // Traits come from the catalog before the info is fetched, from the info afterwards.
        QVERIFY(c->services().catalog().traits()("videotestsrc").isSource);
        QVERIFY(!c->services().catalog().traits()("tee").isSource);
        QVERIFY(need(*c, "audiotestsrc")->entry.isSource);
        QVERIFY(c->services().catalog().traits()("audiotestsrc").isSource);
    }

    void emptyCanvas() {
        QVERIFY(c->plan().empty());
        QVERIFY(c->saveBlockedReason().isEmpty());  // Save always writes
        QVERIFY(!c->canRun());
        QVERIFY(!c->result().valid());
        QCOMPARE(c->launchCommand(), QString());
        QVERIFY(!c->refresh());
    }

    void niceAlignmentLaysOutTheCanvasInOneUndoStep() {
        const Chain chain = makeChain();
        QVERIFY(settle(*c));
        c->setPositions({{chain.src, QPointF(500, 300)}, {chain.convert, QPointF(0, 0)}, {chain.sink, QPointF(250, 600)}});
        auto at = [&](const QString& name) { const auto* e = c->plan().find(toStd(name)); return QPointF(e->x, e->y); };
        const auto revision = c->pipeline().lastChange().executionRevision;
        QSignalSpy done(&c->canvasLayout(), &CanvasLayout::aligned);
        QVERIFY(c->niceAlign());
        QVERIFY(done.wait(30000));
        QVERIFY(done[0][0].toBool());
        // Left to right on one line, nothing on top of anything.
        QVERIFY(at(chain.convert).x() >= at(chain.src).x() + metrics::kBoxWidth);
        QVERIFY(at(chain.sink).x() >= at(chain.convert).x() + metrics::kBoxWidth);
        QVERIFY(qAbs(at(chain.src).y() - at(chain.convert).y()) < 1);
        QCOMPARE(c->pipeline().lastChange().executionRevision, revision);  // presentation only
        // While nothing moves, a link follows the route the layout gave it.
        QVERIFY(c->canvasLayout().fresh());
        QVERIFY(!c->linkViews().front().route.isEmpty());
        QVERIFY(c->undo());  // one step back
        QCOMPARE(at(chain.convert), QPointF(0, 0));
        QVERIFY(!c->canvasLayout().fresh());
        QVERIFY(c->linkViews().front().route.isEmpty());
    }

    void autoAlignKeepsTheSelectionWhereItWas() {
        const Chain chain = makeChain();
        QVERIFY(settle(*c));
        c->setPositions({{chain.src, QPointF(1000, 1000)}, {chain.convert, QPointF(300, 0)}, {chain.sink, QPointF(0, 40)}});
        auto at = [&](const QString& name) { const auto* e = c->plan().find(toStd(name)); return QPointF(e->x, e->y); };
        QSignalSpy done(&c->canvasLayout(), &CanvasLayout::aligned);
        const QPointF centre = c->canvasLayout().pinOf({chain.convert, chain.sink}).centre;
        QVERIFY(c->niceAlign({chain.convert, chain.sink}));
        QVERIFY(done.wait(30000));
        QCOMPARE(at(chain.src), QPointF(1000, 1000));  // not selected: untouched
        QVERIFY(at(chain.sink).x() >= at(chain.convert).x() + metrics::kBoxWidth);
        QVERIFY(QLineF(c->canvasLayout().pinOf({chain.convert, chain.sink}).centre, centre).length() < 1);  // stays put
    }

    // Opening or closing a template keeps it where it is on screen: its centre stays, everything else makes room.
    void aTemplateOpensAndClosesAroundItsCentre() {
        const Chain chain = makeChain();
        QVERIFY(settle(*c));
        QVERIFY(saveTemplate(*c, "Tail", {chain.convert, chain.sink}));
        QVERIFY(settle(*c));
        const QString id = c->outline().collapsedTemplate(chain.convert);
        const QStringList members = c->outline().membersOf(id);
        QSignalSpy done(&c->canvasLayout(), &CanvasLayout::aligned);
        for (bool open : {true, false}) {
            const QPointF centre = c->canvasLayout().pinOf(members).centre;
            c->setTemplateExpanded(id, open);
            QVERIFY(c->canvasLayout().pending() && c->canvasLayout().pending()->gather);  // drawn gathered meanwhile
            QVERIFY(done.wait(30000));
            QVERIFY2(QLineF(c->canvasLayout().pinOf(members).centre, centre).length() < 1, open ? "opened" : "closed");
        }
    }

    // With automatic alignment, an edit is followed by a nice alignment: a dragged box slides back into line.
    void automaticAlignmentFollowsADrag() {
        const Chain chain = makeChain();
        QVERIFY(settle(*c));
        QSignalSpy done(&c->canvasLayout(), &CanvasLayout::aligned);
        QVERIFY(c->niceAlign());
        QVERIFY(done.wait(30000));
        auto at = [&](const QString& name) { const auto* e = c->plan().find(toStd(name)); return QPointF(e->x, e->y); };
        const double line = at(chain.src).y();
        c->services().view().setAutoAlign(true);
        c->setPositions({{chain.convert, at(chain.convert) + QPointF(0, 400)}});
        QVERIFY(done.wait(30000));
        c->services().view().setAutoAlign(false);
        QVERIFY(qAbs(at(chain.convert).y() - at(chain.src).y()) < 1);
        // Off, nothing follows a drag.
        c->setPositions({{chain.convert, at(chain.convert) + QPointF(0, 400)}});
        QVERIFY(!done.wait(1000));
        QVERIFY(at(chain.convert).y() > line + 300);
    }

    void addSourceRunsAnUpdate() {
        need(*c, "videotestsrc");
        QSignalSpy finished(c.get(), &Controller::updateFinished);
        QVERIFY(c->addElement("videotestsrc", withProps({{"pattern", "snow"}})));
        QVERIFY(c->updating());
        QVERIFY(c->busy());
        QVERIFY(c->modified());
        // Preview keeps editing and saving available; running waits for the result.
        QVERIFY(!c->editingBlocked());
        QVERIFY(!c->refresh());
        QVERIFY(!c->cut("videotestsrc_0", "src"));
        QVERIFY(c->saveBlockedReason().isEmpty());
        QVERIFY(!c->canRun());
        QVERIFY(settle(*c));
        QCOMPARE(finished.count(), 1);
        QCOMPARE(fromStd(c->result().status), QString("ok"));
        QVERIFY(c->saveBlockedReason().isEmpty());          // an unfinished pipeline can be saved ...
        QVERIFY(!c->canRun());          // ... but not run (SPEC 8)
        ElementView v = c->view("videotestsrc_0");
        QVERIFY(v.isSource);
        QCOMPARE(v.propLines, QStringList{"pattern = snow"});
        QCOMPARE(v.srcs.size(), 1);
        QCOMPARE(v.srcs[0].state, PadView::Free);
        QCOMPARE(v.srcs[0].media, QString("video/x-raw"));
        QCOMPARE(v.srcs[0].label, QString("src"));
        QVERIFY(v.sinks.isEmpty());
    }

    // Work no source feeds does not run: adding it keeps the last validation, and saving stays open.
    void looseWorkKeepsTheValidation() {
        Chain ch = makeChain();
        QVERIFY(c->saveBlockedReason().isEmpty());
        need(*c, "videoconvert");
        QSignalSpy finished(c.get(), &Controller::updateFinished);
        QVERIFY(c->addElement("videoconvert", {}));
        QVERIFY(!c->busy());
        QCOMPARE(finished.count(), 1);
        QVERIFY(c->result().complete());
        QVERIFY(c->saveBlockedReason().isEmpty());
    }

    void chainIsCompleteAndRuns() {
        Chain ch = makeChain();
        QVERIFY(!ch.sink.isEmpty());
        QVERIFY(c->result().complete());
        QVERIFY(c->saveBlockedReason().isEmpty());
        QVERIFY(c->canRun());
        QCOMPARE(c->view(ch.convert).sinks.size(), 1);
        QCOMPARE(c->view(ch.convert).sinks[0].state, PadView::Linked);
        QCOMPARE(c->view(ch.sink).srcs.size(), 0);

        QSignalSpy log(c.get(), &Controller::logLine);
        QSignalSpy ended(c.get(), &Controller::runFinished);
        QVERIFY(c->run());
        QVERIFY(c->running());
        QVERIFY(!c->saveBlockedReason().isEmpty());
        QVERIFY(!c->addElement("videotestsrc", {}));
        QVERIFY(waitFor([&] { return ended.count() == 1; }));
        QCOMPARE(ended[0][0].toString(), QString("eos"));
        QVERIFY(!c->running());
        QVERIFY(c->canRun());
        bool sawHelperLine = false;
        for (const auto& args : log) sawHelperLine |= args[0].toString().contains("[gps-run] fake gps-run");
        QVERIFY(sawHelperLine);
    }

    void aRunCanBeStopped() {
        QString src = addSource(*c, "videotestsrc");
        QString sink = addAfter(*c, src, "src", "fakesink", "sink", withProps({{"fake-hang", "run"}}));
        QVERIFY(c->canRun());
        QSignalSpy ended(c.get(), &Controller::runFinished);
        QVERIFY(c->run());
        QTest::qWait(300);
        QVERIFY(c->running());  // would be over after 0.3 s without the switch
        c->stopRun();
        QVERIFY(waitFor([&] { return ended.count() == 1; }));
        QCOMPARE(ended[0][0].toString(), QString("interrupted"));
        QVERIFY(!c->running());
        QVERIFY(c->canRun());
    }

    void runIsRefusedWhenIncomplete() {
        addSource(*c, "videotestsrc");
        QVERIFY(!c->run());
        QVERIFY(!c->running());
    }

    void newElementIsPlacedRightOfItsSource() {
        Chain ch = makeChain();
        const gps::Element* s = c->plan().find(ch.src.toStdString());
        const gps::Element* v = c->plan().find(ch.convert.toStdString());
        QCOMPARE(v->x, s->x + 280);  // Graph::addAfter's step: the box is 240 wide, so 40 px are left for the link
    }

    void requestPadOnACompositor() {
        QString first = addSource(*c, "videotestsrc");
        QString mix = addAfter(*c, first, "src", "compositor", "sink_%u");
        QVERIFY(!mix.isEmpty());
        // A Request sink pad exists only while it is linked.
        QCOMPARE(c->view(mix).sinks.size(), 1);
        QCOMPARE(c->view(mix).sinks[0].label, QString("sink_0"));

        QString second = addSource(*c, "videotestsrc");
        need(*c, "compositor");
        QVERIFY(c->linkExisting(second, "src", mix, "sink_%u"));
        QVERIFY(settle(*c));
        ElementView view = c->view(mix);
        QCOMPARE(view.sinks.size(), 2);
        QCOMPARE(fromStd(view.sinks[1].ref.identity), QString("sink_%u#1"));
        QCOMPARE(view.sinks[1].state, PadView::Linked);

        // The pad settings live on the link (xpos, ypos, alpha).
        QVERIFY(c->setCaps(second, "src", "", {{"xpos", "320"}, {"alpha", "0.5"}}));
        QVERIFY(settle(*c));
        QCOMPARE(c->plan().links[1].sinkPadProps.at("xpos"), std::string("320"));
        QVERIFY(text(c->plan().toConf()).contains("padprop.xpos=320"));
        QVERIFY(c->result().ok());
        // Linking a second time would cycle or reuse a src pad: refused with no change.
        QString before = text(c->plan().toConf());
        QVERIFY(!c->linkExisting(second, "src", mix, "sink_%u"));
        QCOMPARE(text(c->plan().toConf()), before);
        // A cycle is refused.
        QString back = addAfter(*c, mix, "src", "videoconvert");
        QVERIFY(!c->linkExisting(back, "src", mix, "sink_%u"));
    }

    void secondSourceDoesNotOverlap() {
        QString a = addSource(*c, "videotestsrc");
        QString b = addSource(*c, "audiotestsrc");
        const gps::Element* ea = c->plan().find(a.toStdString());
        const gps::Element* eb = c->plan().find(b.toStdString());
        QRectF ra(ea->x, ea->y, metrics::kBoxWidth, c->view(a).height());
        QRectF rb(eb->x, eb->y, metrics::kBoxWidth, c->view(b).height());
        QVERIFY(!ra.intersects(rb));
        QVERIFY(c->saveBlockedReason().isEmpty());
    }

    void noOverlapInABiggerGraph() {
        QString src = addSource(*c, "videotestsrc");
        QString tee = addAfter(*c, src, "src", "tee");
        ElementEdit three;
        three.requestSrc["src_%u"] = 3;
        QVERIFY(c->editElement(tee, three));
        QVERIFY(settle(*c));
        QCOMPARE(c->view(tee).srcs.size(), 3);
        QStringList names = {tee};
        for (int k = 0; k < 3; ++k) {
            QString q = addAfter(*c, tee, QString("src_%u#%1").arg(k), "queue");
            QVERIFY2(!q.isEmpty(), "tee branch");
            addAfter(*c, q, "src", "fakesink");
        }
        QVERIFY(c->result().complete());
        QList<QRectF> rects;
        for (const gps::Element& e : c->plan().elements) {
            rects << QRectF(e.x, e.y, metrics::kBoxWidth, c->view(text(e.name)).height());
        }
        QCOMPARE(rects.size(), 8);
        for (int i = 0; i < rects.size(); ++i) {
            for (int j = i + 1; j < rects.size(); ++j) QVERIFY2(!rects[i].intersects(rects[j]), qPrintable(QString("%1 x %2").arg(i).arg(j)));
        }
    }

    void sometimesPadsComeFromTheUpdate() {
        QString file = addSource(*c, "filesrc", withProps({{"location", "/data/a.avi"}}));
        need(*c, "avidemux");
        QVERIFY(c->addAfter(file, "src", "avidemux", "sink", {}));
        // Until the update has run the Sometimes pads are unknown: the plan is the only thing that persists.
        QString demux = text(c->plan().elements.back().name);
        QCOMPARE(c->view(demux).srcs.size(), 0);
        QVERIFY(settle(*c));
        ElementView v = c->view(demux);
        QCOMPARE(v.srcs.size(), 2);
        QCOMPARE(fromStd(v.srcs[0].ref.identity), QString("video_%u#0"));
        QCOMPARE(v.srcs[0].label, QString("video_0"));
        QCOMPARE(v.srcs[0].media, QString("image/jpeg"));
        QCOMPARE(fromStd(v.srcs[1].ref.identity), QString("audio_%u#0"));
        QCOMPARE(v.srcs[1].media, QString("audio/x-raw"));
        QVERIFY(!c->result().complete());

        QString dec = addAfter(*c, demux, "video_%u#0", "jpegdec");
        QVERIFY(!dec.isEmpty());
        QCOMPARE(c->plan().links[1].srcPad, std::string("video_%u#0"));
        QCOMPARE(c->view(demux).srcs[0].state, PadView::Linked);
        QCOMPARE(c->view(demux).srcs[1].state, PadView::Free);
        QCOMPARE(c->view(dec).srcs[0].media, QString("video/x-raw"));
    }

    void unknownDataIsOrange() {
        QString file = addSource(*c, "filesrc", withProps({{"location", "/data/random.bin"}}));
        PadView pad = c->view(file).srcs[0];
        QCOMPARE(pad.state, PadView::Unknown);
        QVERIFY(c->result().ok());
        QVERIFY(!c->result().complete());
    }

    void capsChoiceBecomesABadge() {
        Chain ch = makeChain();
        QVERIFY(c->setCaps(ch.src, "src", "video/x-raw, width=(int)640", {}));
        QVERIFY(settle(*c));
        QCOMPARE(c->linkViews()[0].badge, QString("caps · 1"));
        QCOMPARE(c->linkViews()[0].tip, QString("video/x-raw, width=(int)640"));
        QVERIFY(fromStd(c->result().pad(toStd(ch.src), toStd("src"), true)->caps).contains("width=(int)640"));
        QVERIFY(c->result().complete());
        // Empty text = Auto again.
        QVERIFY(c->setCaps(ch.src, "src", "", {}));
        QVERIFY(settle(*c));
        QCOMPARE(c->linkViews()[0].badge, QString());
    }

    void refusedLinkIsAWarningNotAnError() {
        QString src = addSource(*c, "videotestsrc");
        QString dec = addAfter(*c, src, "src", "jpegdec");  // raw video into a JPEG decoder
        QVERIFY(!dec.isEmpty());
        QCOMPARE(fromStd(c->result().status), QString("ok"));
        QCOMPARE(c->result().refused.size(), 1);
        QVERIFY(!c->result().complete());
        QVERIFY(c->saveBlockedReason().isEmpty());
        QVERIFY(!c->canRun());
        QVERIFY(!c->view(src).note.isEmpty());
        QVERIFY(!c->view(src).isError);
    }

    void queueInsertAndRemove() {
        Chain ch = makeChain();
        QVERIFY(!c->insertQueue(ch.sink, "src"));  // nothing linked there
        need(*c, "queue");
        QVERIFY(c->setCaps(ch.src, "src", "video/x-raw, width=(int)640", {}));
        QVERIFY(settle(*c));
        QVERIFY(c->insertQueue(ch.src, "src"));
        QVERIFY(settle(*c));
        QCOMPARE(c->plan().elements.size(), size_t(4));
        QCOMPARE(c->plan().links.size(), size_t(3));
        QString queue = text(c->plan().elements.back().name);
        QCOMPARE(text(c->plan().find(queue.toStdString())->factory), QString("queue"));
        QVERIFY(c->result().complete());
        QCOMPARE(c->plan().links[0].caps, std::string("video/x-raw, width=(int)640"));  // the choice stays on the first link
        QVERIFY(c->removeQueue(queue));
        QVERIFY(settle(*c));
        QCOMPARE(c->plan().elements.size(), size_t(3));
        QCOMPARE(c->plan().links.size(), size_t(2));
        QCOMPARE(c->plan().links[0].caps, std::string("video/x-raw, width=(int)640"));
        QVERIFY(c->result().complete());
        QVERIFY(!c->removeQueue(ch.convert));  // only a queue can be removed this way
    }

    void cutRemovesOnlyTheLink() {
        Chain ch = makeChain();
        QVERIFY(c->cut(ch.src, "src"));
        QVERIFY(settle(*c));
        // Free-canvas rule: cutting a link only removes the link; downstream elements remain on the canvas as untracked!
        QCOMPARE(c->plan().elements.size(), size_t(3));
        QCOMPARE(c->plan().links.size(), size_t(1));
        QCOMPARE(c->view(ch.src).srcs[0].state, PadView::Free);
        QVERIFY(c->result().ok());
        QVERIFY(!c->cut(ch.src, "src"));  // a free pad has nothing to cut
    }

    void replaceCutsAndAddsBehindTheSamePad() {
        Chain ch = makeChain();
        need(*c, "queue");
        QVERIFY(c->replace(ch.src, "src", "queue", "sink", {}));
        QVERIFY(settle(*c));
        QCOMPARE(c->plan().elements.size(), size_t(3));  // src, sink (untracked), and new queue
        QCOMPARE(text(c->plan().elements.back().factory), QString("queue"));
        QCOMPARE(c->plan().links.size(), size_t(1));
        QCOMPARE(c->plan().links[0].srcElement, ch.src.toStdString());
        QVERIFY(!c->plan().find(ch.convert.toStdString()));
        QVERIFY(c->plan().find(ch.sink.toStdString()));
        QVERIFY(c->result().ok());
        QVERIFY(!c->result().complete());  // the queue's output is free now
    }

    void deletingASourceCascades() {
        QString one = addSource(*c, "videotestsrc");
        QString mix = addAfter(*c, one, "src", "compositor", "sink_%u");
        QString two = addSource(*c, "videotestsrc");
        need(*c, "compositor");
        QVERIFY(c->linkExisting(two, "src", mix, "sink_%u"));
        QVERIFY(settle(*c));
        QString out = addAfter(*c, mix, "src", "fakesink");
        QVERIFY(c->result().complete());

        // Deleting a source only deletes that source; the compositor and sink remain on the free canvas.
        QVERIFY(c->deleteElement(one, false));
        QVERIFY(settle(*c));
        QCOMPARE(c->plan().elements.size(), size_t(3));
        QCOMPARE(c->view(mix).sinks.size(), 1);
        // Deleting the second source removes only that source; mix and out stay on the free canvas as untracked.
        QVERIFY(c->deleteElement(two, false));
        QVERIFY(settle(*c));
        QCOMPARE(c->plan().elements.size(), size_t(2));
        QVERIFY(c->plan().find(mix.toStdString()));
        QVERIFY(c->plan().find(out.toStdString()));
    }

    void aFailedUpdateKeepsTheChange() {
        QString src = addSource(*c, "videotestsrc");
        QVERIFY(c->saveBlockedReason().isEmpty());
        QString sink = addAfter(*c, src, "src", "fakesink", "sink", withProps({{"fake-error", "boom: cannot open"}}));
        QVERIFY(!sink.isEmpty());
        QCOMPARE(fromStd(c->result().status), QString("error"));
        QCOMPARE(fromStd(c->result().errorElement), sink);
        QVERIFY(c->plan().find(sink.toStdString()));   // the change stays
        QVERIFY(c->modified());
        QVERIFY(c->saveBlockedReason().isEmpty());                        // Save still writes ...
        QVERIFY(!c->canRun());                        // ... Run closes
        QTemporaryDir dir;
        QVERIFY(saved(saveAs(*c, dir.filePath("x.conf"))));
        gps::Graph written;
        QVERIFY(gps::Graph::load(toStd(dir.filePath("x.conf")), &written, nullptr));
        QVERIFY(!written.complete);
        QVERIFY(!c->modified());
        ElementView v = c->view(sink);
        QVERIFY(v.isError);
        QCOMPARE(v.note, QString("boom: cannot open"));
        // The user can repair it: the update runs again and Save opens.
        QVERIFY(c->editElement(sink, {}));
        QVERIFY(settle(*c));
        QVERIFY(c->saveBlockedReason().isEmpty());
        QVERIFY(c->canRun());
        QVERIFY(c->view(sink).note.isEmpty());
    }

    void aCrashingHelperIsSurvived() {
        QString src = addSource(*c, "videotestsrc", withProps({{"fake-crash", "1"}}));
        QCOMPARE(fromStd(c->result().status), QString("crashed"));
        QCOMPARE(fromStd(c->result().errorElement), src);
        QVERIFY(fromStd(c->result().message).contains(src));
        QVERIFY(fromStd(c->result().message).contains("stop unexpectedly"));
        QVERIFY(!c->canRun());
        QVERIFY(c->view(src).isError);
        // Still alive: fix the element and carry on.
        QVERIFY(c->editElement(src, {}));
        QVERIFY(settle(*c));
        QCOMPARE(fromStd(c->result().status), QString("ok"));
        QVERIFY(c->saveBlockedReason().isEmpty());
        QVERIFY(!c->canRun());  // a source alone is not complete
    }

    void aHangingHelperIsKilled() {
        c->setUpdateTimeoutMs(300);  // the watchdog fires 2 s later
        QElapsedTimer timer;
        timer.start();
        QString src = addSource(*c, "videotestsrc", withProps({{"fake-hang", "1"}}));
        QCOMPARE(fromStd(c->result().status), QString("hung"));
        QVERIFY(fromStd(c->result().message).contains("did not answer in time"));
        QVERIFY2(timer.elapsed() >= 2300, "killed before timeout + 2 s");
        QVERIFY2(timer.elapsed() < 6000, "watchdog too slow");
        QVERIFY(!c->canRun());
        QVERIFY(c->editElement(src, {}));
        QVERIFY(settle(*c));
        QCOMPARE(fromStd(c->result().status), QString("ok"));
    }

    void aMissingHelperIsReported() {
        need(*c, "videotestsrc");  // cached before the helpers disappear
        struct Restore {
            QByteArray old = qgetenv("GPS_BIN_DIR");
            ~Restore() { qputenv("GPS_BIN_DIR", old); }
        } restore;
        qputenv("GPS_BIN_DIR", "/nonexistent-dir");
        FactoryCatalog lonely;
        QSignalSpy notices(&lonely, &FactoryCatalog::notice);
        lonely.load();
        QVERIFY(waitFor([&] { return notices.count() > 0; }));
        QVERIFY(!lonely.loaded());
        // The element info of the main controller is cached, the update helper is what is missing.
        QVERIFY(c->addElement("videotestsrc", {}));
        QVERIFY(settle(*c));
        QCOMPARE(fromStd(c->result().status), QString("error"));
        QVERIFY(fromStd(c->result().message).contains("gps-update was not found"));
    }

    // The catalog serves every tab: a tab closed while its question is on its way is not called back.
    void aClosedTabIsNotCalledBack() {
        FactoryCatalog catalog;  // its own, so nothing is cached yet
        auto* closed = new QObject;
        bool late = false, answered = false;
        catalog.fetchInfo("videotestsrc", closed, [&](const ElementInfo*) { late = true; });
        catalog.fetchInfos({"videotestsrc"}, c.get(), [&](bool ok) { answered = ok; });
        delete closed;
        QVERIFY(waitFor([&] { return answered; }));
        QVERIFY(!late);
    }

    void cancelLeavesThePlanAlone() {
        // Everything before the last page's Add / Save is a read: info and compatible lists change nothing.
        Chain ch = makeChain();
        QString before = text(c->plan().toConf());
        need(*c, "tee");
        QStringList compatible;
        c->services().catalog().fetchCompatible("video/x-raw", c.get(), [&](const QStringList& l) { compatible = l; });
        QVERIFY(waitFor([&] { return !compatible.isEmpty(); }));
        QVERIFY(compatible.contains("videoconvert"));
        QVERIFY(!compatible.contains("audiotestsrc"));
        c->existingTargets(ch.src, "src");
        QCOMPARE(text(c->plan().toConf()), before);
        QVERIFY(!c->updating());
    }

    void saveAndOpenRoundTrip() {
        Chain ch = makeChain();
        QVERIFY(c->setCaps(ch.src, "src", "video/x-raw, width=(int)640", {}));
        QVERIFY(settle(*c));
        QTemporaryDir dir;
        QString path = dir.filePath("demo.conf");
        QVERIFY(saved(saveAs(*c, path)));
        QVERIFY(!c->modified());
        QCOMPARE(c->target().path(), path);
        gps::Graph persisted; std::string loadError;
        QVERIFY(gps::Graph::load(path.toStdString(), &persisted, &loadError));
        QVERIFY(persisted.complete);
        QVERIFY(c->launchCommand().contains("gst-launch-1.0 demo"));
        QVERIFY(c->launchCommand().contains("GPS_PIPELINE_PATH=" + dir.path()));

        auto other = makeController();
        other->open(std::make_unique<ProjectConfTarget>(path));
        QVERIFY(waitFor([&] { return other->result().valid() && !other->busy(); }));
        QCOMPARE(text(other->plan().toConf()), text(persisted.toConf()));
        QVERIFY(other->result().complete());
        QVERIFY(!other->modified());
        QVERIFY(other->canRun());
        QCOMPARE(other->target().path(), path);
        QCOMPARE(other->linkViews()[0].badge, QString("caps · 1"));

        // "Save" with no path writes to the same file.
        QVERIFY(other->addElement("videotestsrc", {}));
        QVERIFY(settle(*other));
        QVERIFY(other->modified());
        QVERIFY(saved(other->save()));
        QVERIFY(gps::Graph::load(path.toStdString(), &persisted, &loadError));
        QVERIFY(!persisted.complete);
    }

    void openingABrokenFileChangesNothing() {
        Chain ch = makeChain();
        QString before = text(c->plan().toConf());
        QTemporaryDir dir;
        QSignalSpy notices(c.get(), &Controller::notice);

        QFile broken(dir.filePath("broken.conf"));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("this is not a plan");
        broken.close();
        c->open(std::make_unique<ProjectConfTarget>(broken.fileName()));
        QCOMPARE(notices.count(), 1);

        QFile unknown(dir.filePath("unknown.conf"));
        QVERIFY(unknown.open(QIODevice::WriteOnly));
        unknown.write("[gstpipestudio]\nversion=1\nname=x\ncomplete=0\n\n[element:nosuchthing_0]\nfactory=nosuchthing\n");
        unknown.close();
        c->open(std::make_unique<ProjectConfTarget>(unknown.fileName()));
        QVERIFY(waitFor([&] { return notices.count() == 3; }));  // info failure + the open message
        QCOMPARE(text(c->plan().toConf()), before);
        QVERIFY(!c->busy());
        QVERIFY(c->canRun());
    }

    void aPlanWhoseSometimesPadIsGoneReportsPadMissing() {
        QTemporaryDir dir;
        QFile file(dir.filePath("gone.conf"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(
            "[gstpipestudio]\nversion=1\nname=gone\ncomplete=0\n\n"
            "[element:filesrc_0]\nfactory=filesrc\nprop.location=/data/a.avi\n\n"
            "[element:avidemux_1]\nfactory=avidemux\n\n[element:fakesink_2]\nfactory=fakesink\n\n"
            "[link:0]\nsrc=filesrc_0\nsrcpad=src\nsink=avidemux_1\nsinkpad=sink\n\n"
            "[link:1]\nsrc=avidemux_1\nsrcpad=video_%u#3\nsink=fakesink_2\nsinkpad=sink\n");
        file.close();
        c->open(std::make_unique<ProjectConfTarget>(file.fileName()));
        QVERIFY(waitFor([&] { return c->result().valid() && !c->busy(); }));
        QCOMPARE(fromStd(c->result().status), QString("pad-missing"));
        QCOMPARE(c->result().missing.size(), 1);
        QVERIFY(!c->modified());  // loading never makes a document modified
        QVERIFY(!c->canRun());
        QCOMPARE(c->plan().elements.size(), size_t(3));  // loaded as it was written
        // The pad the plan asks for is still drawn, linked, so the user can see what is wrong.
        bool drawn = false;
        for (const PadView& p : c->view("avidemux_1").srcs) drawn |= p.ref.identity == "video_%u#3" && p.state == PadView::Linked;
        QVERIFY(drawn);
    }

    void requestSrcCounts() {
        QString src = addSource(*c, "videotestsrc");
        ElementEdit two;
        two.requestSrc["src_%u"] = 2;
        QString tee = addAfter(*c, src, "src", "tee", "sink", two);
        QCOMPARE(c->view(tee).srcs.size(), 2);
        QCOMPARE(c->view(tee).srcs[0].state, PadView::Free);
        QCOMPARE(c->view(tee).srcs[1].label, QString("src_1"));
        QVERIFY(c->result().ok());

        QString sink = addAfter(*c, tee, "src_%u#1", "fakesink");
        QVERIFY(!sink.isEmpty());
        QSignalSpy notices(c.get(), &Controller::notice);
        ElementEdit none;  // asks for 0 pads: only the linked one (#1) could not go, so it is refused with a notice
        QVERIFY(c->editElement(tee, none));
        QVERIFY(settle(*c));
        QCOMPARE(notices.count(), 1);
        QCOMPARE(c->plan().find(tee.toStdString())->requestSrc.at("src_%u"), 2);
        ElementEdit three;
        three.requestSrc["src_%u"] = 3;
        QVERIFY(c->editElement(tee, three));
        QVERIFY(settle(*c));
        QCOMPARE(c->view(tee).srcs.size(), 3);
    }

    void savingKeepsVersionsAndRecordsTheProject() {
        QTemporaryDir state, dir;
        useUserDir(state.path());
        const QString conf = dir.filePath("demo.conf");
        const QString src = addSource(*c, "videotestsrc");
        QVERIFY(saved(saveAs(*c, conf)));
        QVERIFY(VersionHistory::versions(conf).isEmpty());  // nothing to keep the first time
        QVERIFY(!addAfter(*c, src, "src", "fakesink").isEmpty());
        QVERIFY(saved(c->save()));
        QCOMPARE(VersionHistory::versions(conf).size(), 1);
        QVERIFY(saved(c->save()));  // the same text again: no new version
        QCOMPARE(VersionHistory::versions(conf).size(), 1);

        // The folder is a project: its .gpsproj knows the conf, and the app knows the project.
        const QString project = ProjectFile::pathForConf(conf);
        QVERIFY(QFile::exists(project));
        QCOMPARE(ProjectFile::load(project).confs(), QStringList{"demo.conf"});
        QVERIFY(ProjectRegistry::projects().contains(QFileInfo(project).absoluteFilePath()));

        // An earlier version comes back as an unsaved change to the same file.
        c->restoreVersion(VersionHistory::versions(conf).front().path);
        QVERIFY(waitFor([&] { return !c->busy() && c->plan().elements.size() == 1; }, 10000));
        QVERIFY(c->modified());
        QCOMPARE(c->target().path(), conf);
    }

    void projectHistoryRetainsTemplateContent() {
        QTemporaryDir state, dir;
        useUserDir(state.path());
        const QString conf = dir.filePath("demo.conf");
        ProjectConfTarget target(conf);

        gps::Graph source;
        source.name = "Tail";
        source.revision = 1;
        source.addElement("identity", {{"silent", "true"}});
        gps::Graph first;
        gps::templates::insertTemplate(first, source, 0, 0);
        QVERIFY(saved(target.write(first)));

        source.revision = 2;
        source.elements.front().props["silent"] = "false";
        gps::Graph second;
        gps::templates::insertTemplate(second, source, 0, 0);
        QVERIFY(saved(target.write(second)));
        const auto history = VersionHistory::versions(conf);
        QCOMPARE(history.size(), 1);
        QVERIFY(QDir(dir.filePath("templates")).removeRecursively());
        gps::Graph restored;
        QVERIFY(gps::Graph::load(toStd(history.front().path), &restored, nullptr));
        QCOMPARE(restored.instances.front().revision, 1);
        QCOMPARE(text(restored.elements.front().props.at("silent")), QString("true"));
    }

    void projectSaveReportsMetadataFailureAndCanRetry() {
        QTemporaryDir state, dir;
        useUserDir(state.path());
        const QString conf = dir.filePath("demo.conf");
        const QString manifest = ProjectFile::pathForConf(conf);
        QVERIFY(QDir().mkpath(manifest));
        QVERIFY(!addSource(*c, "videotestsrc").isEmpty());
        const SaveResult partial = saveAs(*c, conf);
        QVERIFY(!partial.ok);
        QCOMPARE(partial.path, conf);
        QVERIFY(partial.error.contains(manifest));
        QVERIFY(QFileInfo::exists(conf));
        QCOMPARE(c->target().path(), conf);
        QVERIFY(c->hasUnsavedWork());

        QVERIFY(QDir().rmdir(manifest));
        QVERIFY(saved(c->save()));
        QVERIFY(!c->hasUnsavedWork());
    }

    // Modified means: not what the last save or load left. Undo back to the save point is not modified any more,
    // and Revert reads the file again.
    void undoBackToTheSavePointIsNotModified() {
        QTemporaryDir dir;
        c->newPlan(std::make_unique<ProjectConfTarget>(dir.filePath("p.conf")));  // New project: no file before Save
        const QString src = addSource(*c, "videotestsrc");
        QVERIFY(c->modified());
        QVERIFY(!c->canRevert());
        QVERIFY(saved(c->save()));
        QVERIFY(!c->modified());
        QVERIFY(!c->canRevert());
        QVERIFY(!addAfter(*c, src, "src", "fakesink").isEmpty());
        QVERIFY(c->hasUnsavedWork());
        QVERIFY(c->undo());
        QVERIFY(!c->modified());
        QVERIFY(c->redo());
        QVERIFY(c->modified());
        QVERIFY(settle(*c));
        QVERIFY(c->canRevert());
        c->revert();
        QVERIFY(waitFor([&] { return !c->busy() && c->plan().elements.size() == 1; }));
        QVERIFY(!c->modified());
        QCOMPARE(c->target().path(), QFileInfo(dir.filePath("p.conf")).absoluteFilePath());
    }

    // A library template keeps being the template it is when one of its versions comes back: Save writes its next
    // revision, never a pipeline.
    void restoringALibraryTemplateVersionKeepsItATemplate() {
        QTemporaryDir state;
        useUserDir(state.path());
        const Chain chain = makeChain();
        QVERIFY(settle(*c));
        QVERIFY(saveTemplate(*c, "Tail", {chain.convert, chain.sink}));
        c->open(std::make_unique<LibraryTemplateTarget>("Tail", c->services().library()));
        QVERIFY(waitFor([&] { return c->target().libraryTemplate() == "Tail" && !c->busy(); }));
        const QString convert = fromStd(c->plan().elements.front().name);
        QVERIFY(c->editElement(convert, withProps({{"qos", "false"}})));
        QVERIFY(settle(*c));
        QVERIFY(saved(c->save()));
        TemplateLibrary& library = c->services().library();
        QCOMPARE(library.content("Tail").revision, 2);
        const QList<VersionHistory::Version> versions = VersionHistory::versions(c->target().path());
        QCOMPARE(versions.size(), 1);
        c->restoreVersion(versions.front().path);
        QVERIFY(waitFor([&] { return !c->busy() && c->modified(); }));
        QCOMPARE(c->target().libraryTemplate(), QString("Tail"));
        QVERIFY(c->canUndo());  // the restore is one step back
        QVERIFY(saved(c->save()));
        QCOMPARE(library.content("Tail").revision, 3);
        QCOMPARE(c->target().libraryTemplate(), QString("Tail"));
        useUserDir({});
    }

    void aProjectKnowsTheTemplatesItUses() {
        QTemporaryDir state, dir;
        useUserDir(state.path());
        const Chain chain = makeChain();
        QVERIFY(settle(*c));
        QVERIFY(saveTemplate(*c, "Tail", {chain.convert, chain.sink}));
        QVERIFY(settle(*c));
        QVERIFY(saved(saveAs(*c, dir.filePath("uses.conf"))));
        const QString project = ProjectFile::pathForConf(dir.filePath("uses.conf"));
        QCOMPARE(ProjectFile::load(project).templates().value("Tail"), 1);
        QCOMPARE(ProjectRegistry::usingTemplate("Tail"), QStringList{QFileInfo(project).absoluteFilePath()});
        QVERIFY(ProjectRegistry::usingTemplate("Other").isEmpty());
        // The conf imports the template like a module; the folder keeps the content it uses.
        QFile conf(dir.filePath("uses.conf"));
        QVERIFY(conf.open(QIODevice::ReadOnly));
        const QString saved = QString::fromUtf8(conf.readAll());
        QVERIFY(saved.contains("template=Tail") && !saved.contains("factory=fakesink"));
        QVERIFY(QFile::exists(dir.filePath("templates/Tail.conf")));
        c->open(std::make_unique<ProjectConfTarget>(dir.filePath("uses.conf")));
        QVERIFY(settle(*c));
        QCOMPARE(c->plan().instances.size(), size_t(1));
        QCOMPARE(c->outline().membersOf(fromStd(c->plan().instances.front().id)).size(), 2);
    }

    // The library is a folder of modules: a template is edited on the canvas (Save is its next revision), renamed,
    // exported, imported and deleted.
    void theLibraryEditsRenamesExportsImportsAndDeletes() {
        QTemporaryDir state, dir;
        useUserDir(state.path());
        const Chain chain = makeChain();
        QVERIFY(settle(*c));
        QVERIFY(saveTemplate(*c, "Tail", {chain.convert, chain.sink}));
        QVERIFY(settle(*c));

        c->open(std::make_unique<LibraryTemplateTarget>("Tail", c->services().library()));
        QVERIFY(waitFor([&] { return c->target().libraryTemplate() == "Tail"; }));
        QVERIFY(settle(*c));
        QCOMPARE(c->plan().elements.size(), size_t(2));
        const QString convert = fromStd(c->plan().elements.front().name);
        QVERIFY(c->editElement(convert, withProps({{"qos", "false"}})));
        QStringList all;
        for (const gps::Element& e : c->plan().elements) all << fromStd(e.name);
        const std::vector<gps::ExposedParam> choices = c->parameterChoices(all);
        QVERIFY(!choices.empty());
        const std::vector<gps::ExposedParam> params{choices.front()};
        c->setTemplateParameters(params);
        QVERIFY(settle(*c));
        QString error;
        QVERIFY(saved(c->save()));
        TemplateLibrary& library = c->services().library();
        QCOMPARE(library.content("Tail").revision, 2);
        QCOMPARE(library.content("Tail").exposed.size(), size_t(1));
        QVERIFY(!c->modified());

        QString clean;
        QVERIFY(library.rename("Tail", "End", &clean, &error));
        QVERIFY(library.content("Tail").elements.empty() && !library.content("End").elements.empty());
        QVERIFY(!library.rename("Nope", "Other", &clean, &error));
        QVERIFY(library.exportTo("End", dir.filePath("End.conf"), &error));
        QVERIFY(!library.importFrom(dir.filePath("End.conf"), &error).size());  // the name is taken
        QVERIFY(library.remove("End"));
        QVERIFY(library.content("End").elements.empty());
        QCOMPARE(library.importFrom(dir.filePath("End.conf"), &error), QString("End"));
        QCOMPARE(library.content("End").revision, 2);
    }

    // Nested templates: an element is drawn in the outermost collapsed copy around it; expanded copies around it are
    // frames, shown only while every copy around them is expanded too.
    void theOutlineOfNestedTemplates() {
        gps::Graph g;
        const std::string src = g.addElement("videotestsrc"), a = g.addElement("queue"), b = g.addElement("identity");
        const std::string sink = g.addElement("fakesink");
        const QString inner = fromStd(gps::templates::wrap(g, {a, b}, "Inner", {}));
        const QString outer = fromStd(gps::templates::wrap(g, {a, b, sink}, "Outer", {}));
        const QString qa = fromStd(a), qb = fromStd(b), qsink = fromStd(sink), qsrc = fromStd(src);

        const Outline collapsed(g, {});
        QCOMPARE(collapsed.boxOf(qa), outer);
        QCOMPARE(collapsed.collapsedTemplate(qsrc), QString());
        QCOMPARE(collapsed.anchorOf(outer), qa);
        QCOMPARE(collapsed.membersOf(outer), QStringList({qa, qb, qsink}));
        QCOMPARE(collapsed.boxesOf({qsrc, qa, qb}), QStringList({qsrc, outer}));
        QCOMPARE(collapsed.expand({qb}), QStringList({qa, qb, qsink}));
        QVERIFY(collapsed.visibleFrames().isEmpty());

        const Outline open(g, {outer});
        QCOMPARE(open.boxOf(qa), inner);
        QCOMPARE(open.boxOf(qsink), qsink);
        QCOMPARE(open.framesAround(qa), QStringList({outer}));
        QCOMPARE(open.expand({qsink, qb}), QStringList({qsink, qa, qb}));
        QCOMPARE(open.visibleFrames(), QStringList({outer}));

        const Outline innerOnly(g, {inner});  // its parent is collapsed: no frame is drawn
        QCOMPARE(innerOnly.boxOf(qa), outer);
        QVERIFY(innerOnly.visibleFrames().isEmpty());
        const Outline both(g, {inner, outer});
        QCOMPARE(both.boxOf(qa), qa);
        QCOMPARE(both.framesAround(qa), QStringList({outer, inner}));
        QCOMPARE(both.visibleFrames().size(), 2);
    }

    // A copy on the canvas is called by its template: renamed in the library, the box and the menus read the new name;
    // the copy's id stays what it was.
    void aTemplateCopyIsCalledByItsTemplatesNewName() {
        QTemporaryDir state;
        useUserDir(state.path());
        const Chain chain = makeChain();
        QVERIFY(settle(*c));
        QVERIFY(saveTemplate(*c, "Tail", {chain.convert, chain.sink}));
        QVERIFY(settle(*c));
        const QString id = c->outline().collapsedTemplate(chain.convert);
        QCOMPARE(fromStd(c->names().instance(toStd(id))), QString("Tail"));
        QString clean, error;
        QVERIFY(c->services().library().rename("Tail", "Ending", &clean, &error));
        QVERIFY(c->plan().instance(toStd(id)) && !id.contains("Ending"));  // the id is not the name
        QCOMPARE(fromStd(c->names().instance(toStd(id))), QString("Ending"));
        QCOMPARE(c->templateView(id).displayTitle, QString("Ending"));
        QCOMPARE(c->lastAction(), QString("Renamed template Tail to Ending"));
        useUserDir({});
    }

    // One file per template: names that share a file name are refused, and a template an earlier version saved under
    // another file name is deleted, and saved again, by its own file.
    void theLibraryKeepsOneFilePerTemplate() {
        QTemporaryDir state;
        useUserDir(state.path());
        TemplateLibrary& library = c->services().library();
        const QDir folder(TemplateLibrary::directory());
        gps::Graph one;
        one.addElement("videotestsrc");
        QString clean, error;
        QVERIFY(library.save("a b", one).ok);
        QVERIFY(!library.acceptName("a_b", &clean, &error));  // a_b.conf is taken
        QVERIFY(library.rename("a b", "a_b", &clean, &error));  // the same file, now under the new name
        QVERIFY(library.plan("a_b") && !library.plan("a b"));
        QCOMPARE(folder.entryList({"*.conf"}), QStringList{"a_b.conf"});

        auto older = [&] {  // "Yüz" as earlier versions named its file
            gps::Graph t = one;
            t.name = "Yüz";
            QVERIFY(t.save(toStd(folder.filePath("Y_z.conf")), nullptr));
            library.reload();
            QVERIFY(library.plan("Yüz"));
        };
        older();
        QVERIFY(library.remove("Yüz"));
        QCOMPARE(folder.entryList({"*.conf"}), QStringList{"a_b.conf"});
        older();
        QVERIFY(library.save("Yüz", one).ok);
        QCOMPARE(folder.entryList({"*.conf"}).size(), 2);  // its older file is gone
        QVERIFY(library.plan("Yüz"));
        useUserDir({});
    }

    // The manifest is written as text through the one writer and read back by QSettings: odd names survive.
    void theProjectManifestReadsBackWhatItWrote() {
        QTemporaryDir dir;
        gps::Graph uses;
        gps::TemplateInstance copy;
        copy.id = "t#0";
        copy.templateName = "Yüz, \"b\"=c";
        copy.revision = 3;
        uses.instances.push_back(copy);
        ProjectFile project = ProjectFile::load(dir.filePath("p.gpsproj"));
        project.record(dir.filePath("my door;1.conf"), uses);
        QVERIFY(project.save());
        const ProjectFile back = ProjectFile::load(dir.filePath("p.gpsproj"));
        QCOMPARE(back.confs(), QStringList{"my door;1.conf"});
        QCOMPARE(back.templates().value("Yüz, \"b\"=c"), 3);
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files), QStringList{"p.gpsproj"});
    }

    // Add after a free src pad, then a template: the copy lands linked, its first free sink pad fed by that pad.
    void addAfterThenATemplateLinksItsFirstFreeSink() {
        QTemporaryDir state;
        useUserDir(state.path());
        const Chain chain = makeChain();
        QVERIFY(settle(*c));
        QVERIFY(saveTemplate(*c, "Tail", {chain.convert, chain.sink}));
        QVERIFY(settle(*c));
        const QString src = addSource(*c, "videotestsrc");
        QVERIFY(!src.isEmpty());
        const QStringList placed = c->placeAfter(src, "src", TemplateLibrary::ref("Tail"));
        QCOMPARE(placed.size(), 2);
        QVERIFY(settle(*c));
        const int link = c->plan().linkFrom(toStd(src), "src");
        QVERIFY(link >= 0);
        const gps::Element* entry = c->plan().find(c->plan().links[static_cast<size_t>(link)].sinkElement);
        QCOMPARE(entry->factory, std::string("videoconvert"));
        QVERIFY(placed.contains(fromStd(entry->name)));
        useUserDir({});
    }

    // A template added after a pad whose next column already holds a box moves down off it, as an element does.
    void aTemplateAfterAPadLandsOffTheBoxThere() {
        QTemporaryDir state;
        useUserDir(state.path());
        const Chain chain = makeChain();
        QVERIFY(settle(*c));
        QVERIFY(saveTemplate(*c, "Tail", {chain.convert, chain.sink}));
        QVERIFY(settle(*c));
        ElementEdit two;
        two.requestSrc = {{"src_%u", 2}};
        const QString src = addSource(*c, "videotestsrc");
        const QString tee = addAfter(*c, src, "src", "tee", "sink", two);
        QVERIFY(!tee.isEmpty());
        const QString first = addAfter(*c, tee, "src_%u#0", "fakesink");
        const double teeY = c->plan().find(toStd(tee))->y;
        QCOMPARE(c->plan().find(toStd(first))->y, teeY);  // the spot after the pad
        const QStringList placed = c->placeAfter(tee, "src_%u#1", TemplateLibrary::ref("Tail"));
        QCOMPARE(placed.size(), 2);
        for (const QString& member : placed) QVERIFY(c->plan().find(toStd(member))->y > teeY);
        useUserDir({});
    }

    void aChangedTemplateCopyAsksThenUpdatesTheLibraryAndTheOtherCopies() {
        QTemporaryDir state, dir;
        useUserDir(state.path());
        const Chain chain = makeChain();
        QVERIFY(settle(*c));
        QVERIFY(saveTemplate(*c, "Tail", {chain.convert, chain.sink}));
        QVERIFY(settle(*c));
        const QString first = c->outline().collapsedTemplate(chain.convert);
        const QStringList second = c->place(TemplateLibrary::ref("Tail"));
        QVERIFY(!second.isEmpty());
        QVERIFY(settle(*c));
        const QString other = c->outline().collapsedTemplate(second.first());
        QVERIFY(saved(saveAs(*c, dir.filePath("old.conf"))));  // holds revision 1

        QSignalSpy asked(c.get(), &Controller::templateChanged);
        c->setTemplateExpanded(first, true);  // inside it, members are plain elements
        QVERIFY(c->editElement(chain.convert, withProps({{"qos", "false"}})));
        QVERIFY(asked.wait(3000));
        QCOMPARE(asked[0][0].toString(), first);
        c->keepEditingTemplate(first);  // "Update template"
        QVERIFY(c->editElement(chain.convert, withProps({{"qos", "false"}, {"dither", "0"}})));
        QTest::qWait(100);
        QCOMPARE(asked.count(), 1);  // asked once per editing session
        QVERIFY(settle(*c));

        c->setTemplateExpanded(first, false);  // collapsing ends the session: the library gets revision 2
        QCOMPARE(c->services().library().content("Tail").revision, 2);
        QVERIFY(waitFor([&] { return c->plan().instance(toStd(other)) && c->plan().instance(toStd(other))->revision == 2; }, 5000));
        bool followed = false;
        for (const QString& m : c->outline().membersOf(other)) {
            const gps::Element* e = c->plan().find(toStd(m));
            followed = followed || (e->factory == "videoconvert" && e->props.count("dither"));
        }
        QVERIFY(followed);  // the other copy took the change
        QVERIFY(settle(*c));

        // A file saved before the change: opening it offers the newer version.
        QSignalSpy outdated(c.get(), &Controller::templatesOutdated);
        c->open(std::make_unique<ProjectConfTarget>(dir.filePath("old.conf")));
        QVERIFY(outdated.wait(10000));
        QCOMPARE(outdated[0][0].toStringList().size(), 2);
        QVERIFY(settle(*c));
        c->services().library().reload();  // read again, as when the catalog arrives: the same revision asks once
        QTest::qWait(100);
        QCOMPARE(outdated.count(), 1);
        QVERIFY(c->updateTemplates(outdated[0][0].toStringList()));
        QVERIFY(!holdsOlderTemplates(*c));
    }

    void dragMovesTheBoxButSavesNothing() {
        QString src = addSource(*c, "videotestsrc");
        QTemporaryDir dir;
        QVERIFY(saved(saveAs(*c, dir.filePath("p.conf"))));
        QSignalSpy changed(c.get(), &Controller::changed);
        c->setPositions({{src, QPointF(512, 384)}});
        QCOMPARE(changed.count(), 1);
        QCOMPARE(c->plan().find(toStd(src))->x, 512.0);
        QVERIFY(!text(c->plan().toConf()).contains("x=512"));  // nice alignment places boxes when a file opens
        QVERIFY(!c->modified());
        QVERIFY(c->canUndo());  // still one step back
        c->setPositions({{src, QPointF(512, 384)}});  // same place: nothing to report
        QCOMPARE(changed.count(), 1);
    }

    void latestEditWinsDuringPreview() {
        need(*c, "videotestsrc");
        QSignalSpy finished(c.get(), &Controller::updateFinished);
        QVERIFY(c->addElement("videotestsrc", withProps({{"fake-slow", "1200"}})));
        const QString source = text(c->plan().elements.front().name);
        QTest::qWait(150); // first helper is running
        QVERIFY(c->updating());
        QVERIFY(c->editElement(source, withProps({{"pattern", "snow"}})));
        QVERIFY(c->editElement(source, withProps({{"pattern", "ball"}})));
        QVERIFY(settle(*c));
        QCOMPARE(finished.count(), 1); // superseded completions do not escape
        QCOMPARE(c->plan().find(toStd(source))->props.at("pattern"), std::string("ball"));
        QCOMPARE(fromStd(c->result().status), QString("ok"));
        QVERIFY(!c->notValidated());
        QVERIFY(c->editElement(source, withProps({{"fake-slow", "1200"}})));
        QTest::qWait(100);
        QVERIFY(c->deleteElement(source));
        QVERIFY(settle(*c));
        QVERIFY(c->plan().empty());
        QVERIFY(!c->result().valid());
    }

    void sharedDocumentAndPresentationHistory() {
        const QString source = addSource(*c, "videotestsrc");
        const auto initial = c->plan().find(toStd(source));
        const double oldX = initial->x, oldY = initial->y;
        const auto executionRevision = c->pipeline().lastChange().executionRevision;
        QSignalSpy completed(c.get(), &Controller::updateFinished);
        c->setPositions({{source, QPointF(-12000, -8000)}});
        QCOMPARE(c->pipeline().graph().find(toStd(source))->x, -12000.0);
        QCOMPARE(c->pipeline().lastChange().executionRevision, executionRevision);
        QVERIFY(!c->updating());
        QCOMPARE(completed.count(), 0);
        QVERIFY(c->undo());
        QCOMPARE(c->plan().find(toStd(source))->x, oldX);
        QCOMPARE(c->plan().find(toStd(source))->y, oldY);
        QVERIFY(!c->updating());
        QVERIFY(c->redo());
        QCOMPARE(c->plan().find(toStd(source))->x, -12000.0);
        QVERIFY(c->refresh()); QVERIFY(settle(*c));
        QCOMPARE(c->plan().find(toStd(source))->x, -12000.0);
        QVERIFY(!c->pipeline().graph().find(toStd(source))->pads.empty());
    }

    void newPlanClearsEverything() {
        makeChain();
        QVERIFY(c->modified());
        c->newPlan(std::make_unique<UntitledTarget>(1));
        QVERIFY(c->plan().empty());
        QVERIFY(!c->modified());
        QVERIFY(c->target().path().isEmpty());
        QVERIFY(!c->result().valid());
    }

    void existingTargetsListFreeSinksAndRequestTemplates() {
        QString src = addSource(*c, "videotestsrc");
        QString mix = addAfter(*c, src, "src", "compositor", "sink_%u");
        QString overlay = addAfter(*c, mix, "src", "textoverlay", "video_sink");
        QString other = addSource(*c, "videotestsrc");
        QList<ExistingTarget> targets = c->existingTargets(other, "src");
        QStringList rows;
        for (const ExistingTarget& t : targets) rows << t.element + "." + t.sinkPad + (t.request ? " (request)" : "");
        // compositor: a Request row; textoverlay: its video_sink is taken, text_sink does not meet video/x-raw.
        QVERIFY2(rows.contains(mix + ".sink_%u (request)"), qPrintable(rows.join(", ")));
        QVERIFY(!rows.contains(overlay + ".video_sink"));
        QVERIFY(!rows.contains(overlay + ".text_sink"));
        // The source itself is never offered (a pad cannot link to its own element).
        for (const QString& r : rows) QVERIFY(!r.startsWith(other + "."));
        // From the compositor's own src a link back to the compositor would cycle.
        for (const ExistingTarget& t : c->existingTargets(mix, "src")) QVERIFY(t.element != mix);
    }

    void freeFormUntrackedSpliceAutoHealAndOuroboros() {
        need(*c, "videotestsrc");
        need(*c, "videoconvert");
        need(*c, "fakesink");

        // 1. Add a FILTER and a SINK without a source: they stay untracked with an ℹ info note, and DO NOT trigger gps-update!
        QVERIFY(c->addElement("videoconvert", {}));
        QVERIFY(!c->updating());
        QString conv = text(c->plan().elements.back().name);
        QVERIFY(!c->view(conv).isTracked);
        QVERIFY(c->view(conv).isInfo);
        QVERIFY(c->view(conv).note.contains("ℹ"));

        QVERIFY(c->addElement("fakesink", {}));
        QVERIFY(!c->updating());
        QString snk = text(c->plan().elements.back().name);
        QVERIFY(!c->view(snk).isTracked);

        // 2. Ouroboros self-loop check emits ironic notice.
        QSignalSpy notices(c.get(), &Controller::notice);
        QVERIFY(!c->linkExisting(conv, "src", conv, "sink"));
        QCOMPARE(notices.count(), 1);
        QVERIFY(notices[0][0].toString().startsWith("Ouroboros!"));

        // 3. Add a source and link it directly to fakesink: both become tracked.
        QString src = addSource(*c, "videotestsrc");
        QVERIFY(c->linkExisting(src, "src", snk, "sink"));
        QVERIFY(settle(*c));
        QVERIFY(c->view(src).isTracked);
        QVERIFY(c->view(snk).isTracked);
        QVERIFY(!c->view(conv).isTracked);

        // 4. Splice videoconvert onto the link src -> snk: adapts into src -> conv -> snk!
        QVERIFY(c->spliceElement(conv, src, "src"));
        QVERIFY(settle(*c));
        QVERIFY(c->view(conv).isTracked);
        QCOMPARE(c->plan().links.size(), size_t(2));

        // 5. Delete videoconvert with Auto-Heal: heals back to src -> snk!
        QVERIFY(c->deleteElement(conv, true));
        QVERIFY(settle(*c));
        QCOMPARE(c->plan().elements.size(), size_t(2));
        QCOMPARE(c->plan().links.size(), size_t(1));
        QCOMPARE(text(c->plan().links[0].srcElement), src);
        QCOMPARE(text(c->plan().links[0].sinkElement), snk);

        // 6. Undo / Redo restores videoconvert and its links!
        QVERIFY(c->canUndo());
        QVERIFY(c->undo());
        QVERIFY(settle(*c));
        QCOMPARE(c->plan().elements.size(), size_t(3));
        QCOMPARE(c->plan().links.size(), size_t(2));
        QVERIFY(c->canRedo());
        QVERIFY(c->redo());
        QVERIFY(settle(*c));
        QCOMPARE(c->plan().elements.size(), size_t(2));
        QCOMPARE(c->plan().links.size(), size_t(1));

        // 7. Disconnect link without pruning downstream elements.
        QVERIFY(c->disconnectLink(src, "src"));
        QVERIFY(settle(*c));
        QCOMPARE(c->plan().elements.size(), size_t(2));
        QVERIFY(!c->view(snk).isTracked);
    }

    void multiSelectionOperationsAndCustomElements() {
        QTemporaryDir customDir;
        useUserDir(customDir.path());

        need(*c, "videotestsrc");
        need(*c, "videoconvert");
        need(*c, "queue");
        need(*c, "fakesink");

        // 1. Add 2 unconnected filter elements and auto-connect them via connectSelectedElements.
        QVERIFY(c->addElement("videoconvert", {}, 100, 100));
        QString conv = text(c->plan().elements.back().name);
        QVERIFY(c->addElement("queue", {}, 360, 100));
        QString scale = text(c->plan().elements.back().name);
        QVERIFY(c->connectSelectedElements({conv, scale}));
        QCOMPARE(c->plan().links.size(), size_t(1));
        QCOMPARE(text(c->plan().links[0].srcElement), conv);
        QCOMPARE(text(c->plan().links[0].sinkElement), scale);

        // 2. Duplicate the connected pair: duplicates both elements AND the internal link between them!
        QStringList duped = c->duplicateElements({conv, scale});
        QCOMPARE(duped.size(), 2);
        QCOMPARE(c->plan().elements.size(), size_t(4));
        QCOMPARE(c->plan().links.size(), size_t(2));

        // 3. Batch delete the duplicated pair in one operation.
        QVERIFY(c->deleteSelection(duped));
        QCOMPARE(c->plan().elements.size(), size_t(2));
        QCOMPARE(c->plan().links.size(), size_t(1));

        // 4. Save the connected pair (conv -> scale) as a reusable Template ("ScaleConvert").
        QVERIFY(saveTemplate(*c, "ScaleConvert", {conv, scale}));
        const QList<ComponentInfo> templates = c->services().library().components();
        QCOMPARE(templates.size(), 1);
        QCOMPARE(templates.first().title, QString("ScaleConvert"));
        QCOMPARE(templates.first().hint, QString("template · 2"));
        QVERIFY(templates.first().takesInput);
        QVERIFY(templates.first().givesOutput);
        QVERIFY(templates.first().role == gps::Role::Filter);

        // The pair on canvas is now grouped as a collapsed Template with 1 external sink and 1 external src!
        QString grp = c->outline().collapsedTemplate(conv);
        QVERIFY(!grp.isEmpty());
        QVERIFY(!c->isTemplateExpanded(grp));
        ElementView gv = c->templateView(grp);
        QVERIFY(gv.isTemplate);
        QCOMPARE(gv.displayTitle, QString("ScaleConvert"));
        QCOMPARE(gv.sinks.size(), 1);
        QCOMPARE(gv.srcs.size(), 1);
        QCOMPARE(fromStd(gv.sinks.first().ref.element), conv);
        QCOMPARE(fromStd(gv.srcs.first().ref.element), scale);

        // 5. Add a connected pipeline src -> snk, then splice the entire 2-element Template onto that wire!
        QString src = addSource(*c, "videotestsrc");
        QVERIFY(c->addElement("fakesink", {}, 700, 100));
        QString snk = text(c->plan().elements.back().name);
        QVERIFY(c->linkExisting(src, "src", snk, "sink"));
        QVERIFY(settle(*c));

        QVERIFY(c->spliceElement(conv, src, "src"));
        QVERIFY(settle(*c));
        // Now the pipeline is src -> conv -> scale -> snk (3 links)!
        QCOMPARE(c->plan().links.size(), size_t(3));
        QVERIFY(c->result().complete());

        // 6. Delete the collapsed Template: auto-heals across both inner elements back to src -> snk!
        QVERIFY(c->deleteElement(conv, true));
        QVERIFY(settle(*c));
        QCOMPARE(c->plan().elements.size(), size_t(2));
        QCOMPARE(c->plan().links.size(), size_t(1));
        QCOMPARE(text(c->plan().links[0].srcElement), src);
        QCOMPARE(text(c->plan().links[0].sinkElement), snk);

        // 7. Re-insert the saved Template from the library via place.
        QStringList inserted = c->place(TemplateLibrary::ref("ScaleConvert"), 250, 250);
        QCOMPARE(inserted.size(), 2);
        QCOMPARE(c->plan().elements.size(), size_t(4));
        QCOMPARE(c->plan().links.size(), size_t(2));

        // 8. Ungroup the inserted pair, disconnect them, then splice the multi-selection directly onto src -> snk!
        QString insGrp = c->outline().collapsedTemplate(inserted.first());
        QVERIFY(c->detachTemplate(insGrp));
        QVERIFY(c->disconnectBetweenElements(inserted));
        QCOMPARE(c->plan().links.size(), size_t(1));  // only src -> snk
        const auto beforeRejectedSplice = c->plan().toConf();
        QVERIFY(!c->spliceElements(inserted, src, "src"));
        QCOMPARE(c->plan().toConf(), beforeRejectedSplice);
        QVERIFY(c->connectSelectedElements(inserted));
        QVERIFY(c->spliceElements(inserted, src, "src"));
        QVERIFY(settle(*c));
        // Both explicitly connected elements were spliced into src -> snk!
        QCOMPARE(c->plan().links.size(), size_t(3));
        QVERIFY(c->result().complete());
        // Deleting the multi-selection auto-heals back to src -> snk!
        QVERIFY(c->deleteSelection(inserted, {}, true));
        QVERIFY(settle(*c));
        QCOMPARE(c->plan().elements.size(), size_t(2));
        QCOMPARE(c->plan().links.size(), size_t(1));
    }
};

QTEST_MAIN(ControllerTest)
#include "controller_test.moc"
