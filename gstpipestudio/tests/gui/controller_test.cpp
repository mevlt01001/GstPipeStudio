// controller_test.cpp – every flow of SPEC 8 driven through the Controller against the fake helpers.
#include <QSignalSpy>
#include <QTemporaryDir>

#include "testsupport.h"

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
        QVERIFY(c->catalogLoaded());
        auto find = [&](const QString& f) {
            for (const ListEntry& e : c->catalog()) {
                if (e.factory == f) return e;
            }
            return ListEntry();
        };
        QVERIFY(find("videotestsrc").isSource);
        QVERIFY(!find("videotestsrc").nvidia);
        QVERIFY(find("nvvideoconvert").nvidia);
        QVERIFY(find("tee").hasRequestSrc);
        // Traits come from the catalog before the info is fetched, from the info afterwards.
        QVERIFY(c->traits()("videotestsrc").isSource);
        QVERIFY(!c->traits()("tee").isSource);
        QVERIFY(need(*c, "audiotestsrc")->isSource);
        QVERIFY(c->traits()("audiotestsrc").isSource);
    }

    void emptyCanvas() {
        QVERIFY(c->plan().empty());
        QVERIFY(!c->canSave());
        QVERIFY(!c->canRun());
        QVERIFY(!c->result().valid());
        QCOMPARE(c->launchCommand(), QString());
        QVERIFY(!c->refresh());
    }

    void arrangingPipelineKeepsExecutionAndCanUndo() {
        const Chain chain = makeChain();
        QVERIFY(settle(*c));
        const auto before = c->plan().find(toStd(chain.convert))->x;
        const auto revision = c->pipeline().lastChange().executionRevision;
        QVERIFY(c->arrangePipeline());
        const auto* source = c->plan().find(toStd(chain.src));
        const auto* convert = c->plan().find(toStd(chain.convert));
        const auto* sink = c->plan().find(toStd(chain.sink));
        QVERIFY(convert->x - source->x >= 500);
        QVERIFY(sink->x - convert->x >= 500);
        QCOMPARE(c->pipeline().lastChange().executionRevision, revision);
        QVERIFY(!c->updating());
        QVERIFY(c->undo());
        QCOMPARE(c->plan().find(toStd(chain.convert))->x, before);
    }

    void addSourceRunsAnUpdate() {
        need(*c, "videotestsrc");
        QSignalSpy finished(c.get(), &Controller::updateFinished);
        QVERIFY(c->addSource("videotestsrc", withProps({{"pattern", "snow"}})));
        QVERIFY(c->updating());
        QVERIFY(c->busy());
        QVERIFY(c->dirty());
        // Preview keeps editing available; saving/running waits for the result.
        QVERIFY(!c->editingBlocked());
        QVERIFY(!c->refresh());
        QVERIFY(!c->cut("videotestsrc_0", "src"));
        QVERIFY(!c->canSave());
        QVERIFY(settle(*c));
        QCOMPARE(finished.count(), 1);
        QCOMPARE(c->result().status, QString("ok"));
        QVERIFY(c->canSave());          // an unfinished pipeline can be saved ...
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

    void chainIsCompleteAndRuns() {
        Chain ch = makeChain();
        QVERIFY(!ch.sink.isEmpty());
        QVERIFY(c->result().isComplete());
        QVERIFY(c->canSave());
        QVERIFY(c->canRun());
        QCOMPARE(c->view(ch.convert).sinks.size(), 1);
        QCOMPARE(c->view(ch.convert).sinks[0].state, PadView::Linked);
        QCOMPARE(c->view(ch.sink).srcs.size(), 0);

        QSignalSpy log(c.get(), &Controller::logLine);
        QSignalSpy ended(c.get(), &Controller::runFinished);
        QVERIFY(c->run());
        QVERIFY(c->running());
        QVERIFY(!c->canSave());
        QVERIFY(!c->addSource("videotestsrc", {}));
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
        QCOMPARE(view.sinks[1].identity, QString("sink_%u#1"));
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
        QVERIFY(c->canSave());
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
        QVERIFY(c->result().isComplete());
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
        QCOMPARE(v.srcs[0].identity, QString("video_%u#0"));
        QCOMPARE(v.srcs[0].label, QString("video_0"));
        QCOMPARE(v.srcs[0].media, QString("image/jpeg"));
        QCOMPARE(v.srcs[1].identity, QString("audio_%u#0"));
        QCOMPARE(v.srcs[1].media, QString("audio/x-raw"));
        QVERIFY(!c->result().complete);

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
        QVERIFY(!c->result().isComplete());
    }

    void capsChoiceBecomesABadge() {
        Chain ch = makeChain();
        QVERIFY(c->setCaps(ch.src, "src", "video/x-raw, width=(int)640", {}));
        QVERIFY(settle(*c));
        QCOMPARE(c->linkViews()[0].badge, QString("caps · 1"));
        QCOMPARE(c->linkViews()[0].tip, QString("video/x-raw, width=(int)640"));
        QVERIFY(c->result().pad(ch.src, "src", true)->caps.contains("width=(int)640"));
        QVERIFY(c->result().isComplete());
        // Empty text = Auto again.
        QVERIFY(c->setCaps(ch.src, "src", "", {}));
        QVERIFY(settle(*c));
        QCOMPARE(c->linkViews()[0].badge, QString());
    }

    void refusedLinkIsAWarningNotAnError() {
        QString src = addSource(*c, "videotestsrc");
        QString dec = addAfter(*c, src, "src", "jpegdec");  // raw video into a JPEG decoder
        QVERIFY(!dec.isEmpty());
        QCOMPARE(c->result().status, QString("ok"));
        QCOMPARE(c->result().refused.size(), 1);
        QVERIFY(!c->result().isComplete());
        QVERIFY(c->canSave());
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
        QVERIFY(c->result().isComplete());
        QCOMPARE(c->plan().links[0].caps, std::string("video/x-raw, width=(int)640"));  // the choice stays on the first link
        QVERIFY(c->removeQueue(queue));
        QVERIFY(settle(*c));
        QCOMPARE(c->plan().elements.size(), size_t(3));
        QCOMPARE(c->plan().links.size(), size_t(2));
        QCOMPARE(c->plan().links[0].caps, std::string("video/x-raw, width=(int)640"));
        QVERIFY(c->result().isComplete());
        QVERIFY(!c->removeQueue(ch.convert));  // only a queue can be removed this way
    }

    void cutShowsWhatWillGoAndThenCuts() {
        Chain ch = makeChain();
        QString before = text(c->plan().toConf());
        gps::Removal preview = c->previewCut(ch.src, "src");
        QVERIFY(preview.elements.empty());
        QCOMPARE(preview.links, 1);
        QCOMPARE(text(c->plan().toConf()), before);  // a preview changes nothing
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
        QCOMPARE(c->previewReplace(ch.src, "src").elements, std::vector<std::string>{ch.convert.toStdString()});
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
        QVERIFY(!c->result().isComplete());  // the queue's output is free now
    }

    void deletingASourceCascades() {
        QString one = addSource(*c, "videotestsrc");
        QString mix = addAfter(*c, one, "src", "compositor", "sink_%u");
        QString two = addSource(*c, "videotestsrc");
        need(*c, "compositor");
        QVERIFY(c->linkExisting(two, "src", mix, "sink_%u"));
        QVERIFY(settle(*c));
        QString out = addAfter(*c, mix, "src", "fakesink");
        QVERIFY(c->result().isComplete());

        // Deleting a source only deletes that source; the compositor and sink remain on the free canvas.
        QCOMPARE(c->previewDeleteSource(one).elements, std::vector<std::string>{one.toStdString()});
        QVERIFY(c->deleteSource(one));
        QVERIFY(settle(*c));
        QCOMPARE(c->plan().elements.size(), size_t(3));
        QCOMPARE(c->view(mix).sinks.size(), 1);
        // Deleting the second source removes only that source; mix and out stay on the free canvas as untracked.
        QCOMPARE(c->previewDeleteSource(two).elements, std::vector<std::string>{two.toStdString()});
        QVERIFY(c->deleteSource(two));
        QVERIFY(settle(*c));
        QCOMPARE(c->plan().elements.size(), size_t(2));
        QVERIFY(c->plan().find(mix.toStdString()));
        QVERIFY(c->plan().find(out.toStdString()));
    }

    void aFailedUpdateKeepsTheChange() {
        QString src = addSource(*c, "videotestsrc");
        QVERIFY(c->canSave());
        QString sink = addAfter(*c, src, "src", "fakesink", "sink", withProps({{"fake-error", "boom: cannot open"}}));
        QVERIFY(!sink.isEmpty());
        QCOMPARE(c->result().status, QString("error"));
        QCOMPARE(c->result().errorElement, sink);
        QVERIFY(c->plan().find(sink.toStdString()));   // the change stays
        QVERIFY(c->dirty());
        QVERIFY(!c->canSave());                       // Save closes
        QVERIFY(!c->canRun());                        // Run closes
        QTemporaryDir dir;
        QString error;
        QVERIFY(!c->save(dir.filePath("x.conf"), &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFile::exists(dir.filePath("x.conf")));
        ElementView v = c->view(sink);
        QVERIFY(v.isError);
        QCOMPARE(v.note, QString("boom: cannot open"));
        // The user can repair it: the update runs again and Save opens.
        QVERIFY(c->editElement(sink, {}));
        QVERIFY(settle(*c));
        QVERIFY(c->canSave());
        QVERIFY(c->canRun());
        QVERIFY(c->view(sink).note.isEmpty());
    }

    void aCrashingHelperIsSurvived() {
        QString src = addSource(*c, "videotestsrc", withProps({{"fake-crash", "1"}}));
        QCOMPARE(c->result().status, QString("crashed"));
        QCOMPARE(c->result().errorElement, src);
        QVERIFY(c->result().message.contains(src));
        QVERIFY(c->result().message.contains("stop unexpectedly"));
        QVERIFY(!c->canSave());
        QVERIFY(c->view(src).isError);
        // Still alive: fix the element and carry on.
        QVERIFY(c->editElement(src, {}));
        QVERIFY(settle(*c));
        QCOMPARE(c->result().status, QString("ok"));
        QVERIFY(c->canSave());
    }

    void aHangingHelperIsKilled() {
        c->setUpdateTimeoutMs(300);  // the watchdog fires 2 s later
        QElapsedTimer timer;
        timer.start();
        QString src = addSource(*c, "videotestsrc", withProps({{"fake-hang", "1"}}));
        QCOMPARE(c->result().status, QString("hung"));
        QVERIFY(c->result().message.contains("did not answer in time"));
        QVERIFY2(timer.elapsed() >= 2300, "killed before timeout + 2 s");
        QVERIFY2(timer.elapsed() < 6000, "watchdog too slow");
        QVERIFY(!c->canSave());
        QVERIFY(c->editElement(src, {}));
        QVERIFY(settle(*c));
        QCOMPARE(c->result().status, QString("ok"));
    }

    void aMissingHelperIsReported() {
        need(*c, "videotestsrc");  // cached before the helpers disappear
        struct Restore {
            QByteArray old = qgetenv("GPS_BIN_DIR");
            ~Restore() { qputenv("GPS_BIN_DIR", old); }
        } restore;
        qputenv("GPS_BIN_DIR", "/nonexistent-dir");
        Controller lonely;
        QSignalSpy notices(&lonely, &Controller::notice);
        lonely.loadCatalog();
        QVERIFY(waitFor([&] { return notices.count() > 0; }));
        QVERIFY(!lonely.catalogLoaded());
        // The element info of the main controller is cached, the update helper is what is missing.
        QVERIFY(c->addSource("videotestsrc", {}));
        QVERIFY(settle(*c));
        QCOMPARE(c->result().status, QString("error"));
        QVERIFY(c->result().message.contains("gps-update was not found"));
    }

    void cancelLeavesThePlanAlone() {
        // Everything before the last page's Add / Save is a read: previews, info and compatible lists change nothing.
        Chain ch = makeChain();
        QString before = text(c->plan().toConf());
        c->previewCut(ch.src, "src");
        c->previewReplace(ch.src, "src");
        c->previewDeleteSource(ch.src);
        need(*c, "tee");
        QStringList compatible;
        c->fetchCompatible("video/x-raw", [&](const QStringList& l) { compatible = l; });
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
        QString error;
        QVERIFY2(c->save(path, &error), qPrintable(error));
        QVERIFY(!c->dirty());
        QCOMPARE(c->filePath(), path);
        gps::Graph persisted; std::string loadError;
        QVERIFY(gps::Graph::load(path.toStdString(), &persisted, &loadError));
        QVERIFY(persisted.complete);
        QVERIFY(c->launchCommand().contains("gst-launch-1.0 demo"));
        QVERIFY(c->launchCommand().contains("GPS_PIPELINE_PATH=" + dir.path()));

        auto other = makeController();
        other->open(path);
        QVERIFY(waitFor([&] { return other->result().valid() && !other->busy(); }));
        QCOMPARE(text(other->plan().toConf()), text(persisted.toConf()));
        QVERIFY(other->result().isComplete());
        QVERIFY(!other->dirty());
        QVERIFY(other->canRun());
        QCOMPARE(other->filePath(), path);
        QCOMPARE(other->linkViews()[0].badge, QString("caps · 1"));

        // "Save" with no path writes to the same file.
        QVERIFY(other->addSource("videotestsrc", {}));
        QVERIFY(settle(*other));
        QVERIFY(other->dirty());
        QVERIFY(other->save("", &error));
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
        c->open(broken.fileName());
        QCOMPARE(notices.count(), 1);

        QFile unknown(dir.filePath("unknown.conf"));
        QVERIFY(unknown.open(QIODevice::WriteOnly));
        unknown.write("[gstpipestudio]\nversion=1\nname=x\ncomplete=0\n\n[element:nosuchthing_0]\nfactory=nosuchthing\n");
        unknown.close();
        c->open(unknown.fileName());
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
        c->open(file.fileName());
        QVERIFY(waitFor([&] { return c->result().valid() && !c->busy(); }));
        QCOMPARE(c->result().status, QString("pad-missing"));
        QCOMPARE(c->result().missing.size(), 1);
        QVERIFY(!c->canSave());
        QVERIFY(!c->canRun());
        QCOMPARE(c->plan().elements.size(), size_t(3));  // loaded as it was written
        // The pad the plan asks for is still drawn, linked, so the user can see what is wrong.
        bool drawn = false;
        for (const PadView& p : c->view("avidemux_1").srcs) drawn |= p.identity == "video_%u#3" && p.state == PadView::Linked;
        QVERIFY(drawn);
    }

    void exposingParameters() {
        ElementEdit edit;
        edit.props = {{"num-buffers", "100"}};
        edit.exposed = {"num-buffers"};
        QString one = addSource(*c, "videotestsrc", edit);
        QCOMPARE(c->plan().exposed.size(), size_t(1));
        QCOMPARE(c->plan().exposed[0].name, std::string("num-buffers"));
        QCOMPARE(c->plan().exposed[0].element, one.toStdString());
        QCOMPARE(c->view(one).propLines, QStringList{"↗ num-buffers = 100"});

        QString two = addSource(*c, "videotestsrc", edit);
        QCOMPARE(c->plan().exposed.size(), size_t(2));
        QCOMPARE(c->plan().exposed[1].name, std::string("num-buffers_2"));  // unique

        // Editing keeps the name an exposure already has.
        QVERIFY(c->editElement(one, edit));
        QVERIFY(settle(*c));
        QCOMPARE(c->plan().exposed[1].name, std::string("num-buffers_2"));
        QCOMPARE(c->plan().exposed.size(), size_t(2));
        QVERIFY(text(c->plan().toConf()).contains("[expose:num-buffers_2]"));

        QVERIFY(c->editElement(two, withProps({{"num-buffers", "100"}})));  // no longer exposed
        QVERIFY(settle(*c));
        QCOMPARE(c->plan().exposed.size(), size_t(1));

        QTemporaryDir dir;
        QString error;
        QVERIFY(c->save(dir.filePath("p.conf"), &error));
        QVERIFY(c->launchCommand().endsWith("gst-launch-1.0 p num-buffers=VALUE"));
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

    void dragSavesThePosition() {
        QString src = addSource(*c, "videotestsrc");
        QSignalSpy changed(c.get(), &Controller::changed);
        c->setPosition(src, 512, 384);
        QCOMPARE(changed.count(), 1);
        QVERIFY(text(c->plan().toConf()).contains("x=512"));
        QVERIFY(text(c->plan().toConf()).contains("y=384"));
        c->setPosition(src, 512, 384);  // same place: nothing to report
        QCOMPARE(changed.count(), 1);
    }

    void latestEditWinsDuringPreview() {
        need(*c, "videotestsrc");
        QSignalSpy finished(c.get(), &Controller::updateFinished);
        QVERIFY(c->addSource("videotestsrc", withProps({{"fake-slow", "1200"}})));
        const QString source = text(c->plan().elements.front().name);
        QTest::qWait(150); // first helper is running
        QVERIFY(c->updating());
        QVERIFY(c->editElement(source, withProps({{"pattern", "snow"}})));
        QVERIFY(c->editElement(source, withProps({{"pattern", "ball"}})));
        QVERIFY(settle(*c));
        QCOMPARE(finished.count(), 1); // superseded completions do not escape
        QCOMPARE(c->plan().find(toStd(source))->props.at("pattern"), std::string("ball"));
        QCOMPARE(c->result().status, QString("ok"));
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
        c->setPosition(source, -12000, -8000);
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
        QVERIFY(c->dirty());
        c->newPlan();
        QVERIFY(c->plan().empty());
        QVERIFY(!c->dirty());
        QVERIFY(c->filePath().isEmpty());
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
        qputenv("GPS_CUSTOM_ELEMENTS_DIR", customDir.path().toUtf8());

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

        // 4. Save the connected pair (conv -> scale) as a reusable Custom Element ("ScaleConvert").
        QVERIFY(c->saveCustomElement("ScaleConvert", {conv, scale}));
        QCOMPARE(c->customElements().size(), 1);
        QCOMPARE(c->customElements().first().name, QString("ScaleConvert"));
        QCOMPARE(c->customElements().first().elementCount, 2);
        QVERIFY(c->customElements().first().hasFreeSink);
        QVERIFY(c->customElements().first().hasFreeSrc);

        // The pair on canvas is now grouped as a collapsed Custom Element with 1 external sink and 1 external src!
        QString grp = c->elementGroup(conv);
        QVERIFY(!grp.isEmpty());
        QVERIFY(!c->isGroupExpanded(grp));
        ElementView gv = c->groupView(grp);
        QVERIFY(gv.isCustomGroup);
        QCOMPARE(gv.displayTitle, QString("ScaleConvert"));
        QCOMPARE(gv.sinks.size(), 1);
        QCOMPARE(gv.srcs.size(), 1);
        QCOMPARE(gv.sinks.first().element, conv);
        QCOMPARE(gv.srcs.first().element, scale);

        // 5. Add a connected pipeline src -> snk, then splice the entire 2-element Custom Element onto that wire!
        QString src = addSource(*c, "videotestsrc");
        QVERIFY(c->addElement("fakesink", {}, 700, 100));
        QString snk = text(c->plan().elements.back().name);
        QVERIFY(c->linkExisting(src, "src", snk, "sink"));
        QVERIFY(settle(*c));

        QVERIFY(c->spliceElement(conv, src, "src"));
        QVERIFY(settle(*c));
        // Now the pipeline is src -> conv -> scale -> snk (3 links)!
        QCOMPARE(c->plan().links.size(), size_t(3));
        QVERIFY(c->result().isComplete());

        // 6. Delete the collapsed Custom Element: auto-heals across both inner elements back to src -> snk!
        QVERIFY(c->deleteElement(conv, true));
        QVERIFY(settle(*c));
        QCOMPARE(c->plan().elements.size(), size_t(2));
        QCOMPARE(c->plan().links.size(), size_t(1));
        QCOMPARE(text(c->plan().links[0].srcElement), src);
        QCOMPARE(text(c->plan().links[0].sinkElement), snk);

        // 7. Re-insert the saved Custom Element from catalog via addCustomElement.
        QStringList inserted = c->addCustomElement("ScaleConvert", 250, 250);
        QCOMPARE(inserted.size(), 2);
        QCOMPARE(c->plan().elements.size(), size_t(4));
        QCOMPARE(c->plan().links.size(), size_t(2));

        // 8. Ungroup the inserted pair, disconnect them, then splice the multi-selection directly onto src -> snk!
        QString insGrp = c->elementGroup(inserted.first());
        QVERIFY(c->ungroupElements(insGrp));
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
        QVERIFY(c->result().isComplete());
        // Deleting the multi-selection auto-heals back to src -> snk!
        QVERIFY(c->deleteSelection(inserted, {}, true));
        QVERIFY(settle(*c));
        QCOMPARE(c->plan().elements.size(), size_t(2));
        QCOMPARE(c->plan().links.size(), size_t(1));
    }
};

QTEST_MAIN(ControllerTest)
#include "controller_test.moc"
