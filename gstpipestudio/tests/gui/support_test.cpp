// support_test.cpp – the two widget-free classes of the validation work: SupportReport (the prefilled GitHub issue)
// and SlowWatch (the 60 s / +15 s question with a clock the test moves).
#include <QSignalSpy>
#include <QUrlQuery>
#include <QtTest>

#include "slowwatch.h"
#include "supportreport.h"

namespace {

SupportInfo sample() {
    SupportInfo i;
    i.title = "Validation is taking longer than expected";
    i.action = "Added nvinfer after /home/alice/work/nvstreammux_1.src";
    i.linking = "nvstreammux_1.src (nvstreammux) -> nvinfer_2.sink (nvinfer)";
    i.validation = "error – could not open /home/alice/models/yolo.engine on box7";
    i.appVersion = "0.1.0";
    i.os = "Ubuntu 22.04.5 LTS";
    i.arch = "x86_64";
    i.qt = "6.8.3";
    i.gstreamer = "1.20.3";
    i.nvidiaElements = 57;
    i.driver = "535.183.01";
    i.home = "/home/alice";
    i.user = "alice";
    i.host = "box7";
    for (int n = 1; n <= 15; ++n) i.logTail << QString("log line %1 from /home/alice/run").arg(n);
    return i;
}

QString decodedField(const QUrl& url, const QString& name) { return QUrlQuery(url).queryItemValue(name, QUrl::FullyDecoded); }

struct Capture : UrlOpener {
    QList<QUrl> urls;
    bool open(const QUrl& url) override {
        urls << url;
        return true;
    }
};

}  // namespace

class SupportTest : public QObject {
    Q_OBJECT

private slots:
    // ---- SupportReport

    void theIssueUrlCarriesTitleAndBodyAndParsesBack() {
        SupportInfo info = sample();
        QUrl url = SupportReport::url(info);
        QCOMPARE(url.scheme(), QString("https"));
        QCOMPARE(url.host(), QString("github.com"));
        QCOMPARE(url.path(), QString("/mevlt01001/GstPipeStudio/issues/new"));
        QCOMPARE(decodedField(url, "title"), info.title);
        QString body = decodedField(url, "body");
        for (const char* part : {"Added nvinfer after", "GstPipeStudio: 0.1.0", "Ubuntu 22.04.5 LTS (x86_64)", "Qt: 6.8.3",
                                 "GStreamer: 1.20.3 · NVIDIA elements: 57", "NVIDIA driver: 535.183.01",
                                 "nvstreammux_1.src (nvstreammux) -> nvinfer_2.sink (nvinfer)", "Last validation: error", "log line 15"}) {
            QVERIFY2(body.contains(part), part);
        }
        // Markdown with characters that need care in a URL: & = # + % and a line break survive the round trip.
        info.action = "a&b=c #1 100% 1+1\nsecond line";
        QVERIFY(decodedField(SupportReport::url(info), "body").contains("a&b=c #1 100% 1+1\nsecond line"));
    }

    void homeIsReplacedAndNeitherUserNorHostAppear() {
        SupportInfo info = sample();
        QString body = decodedField(SupportReport::url(info), "body");
        QVERIFY(!body.contains("/home/alice"));
        QVERIFY(!body.contains("alice"));
        QVERIFY(!body.contains("box7"));
        QVERIFY(body.contains("~/work/nvstreammux_1.src"));
        QVERIFY(body.contains("~/models/yolo.engine"));
        QVERIFY(body.contains("<host>"));
        QString title = decodedField(SupportReport::url(info), "title");
        info.title = "Problem for alice on box7 in /home/alice";
        title = decodedField(SupportReport::url(info), "title");
        QCOMPARE(title, QString("Problem for <user> on <host> in ~"));
        // The real machine: whatever systemInfo reports as home, user and host is hidden too.
        SupportInfo real = SupportReport::systemInfo();
        QVERIFY(!real.os.isEmpty() && !real.qt.isEmpty() && !real.arch.isEmpty());
        real.action = "opened " + real.home + "/x.conf";
        QVERIFY(!SupportReport::body(real).contains(real.home + "/x.conf") || real.home.size() <= 1);
    }

    void aHomeOfAnotherProcessIsHiddenToo() {
        // A container or sudo run has HOME=/tmp/x while the paths the user typed are /home/<name>/...
        SupportInfo info = sample();
        info.home = "/tmp/gps-home";
        info.user.clear();
        info.action = "opened /home/bob/Downloads/a b.deb and /Users/carol/x.conf (/home/dave/y)";
        QString body = decodedField(SupportReport::url(info), "body");
        QVERIFY(!body.contains("bob") && !body.contains("carol") && !body.contains("dave"));
        QVERIFY(body.contains("~/Downloads/a"));
        QVERIFY(body.contains("~/x.conf"));
    }

    void theUrlStaysWithinTheLimitAndTheLogTailIsCutFirst() {
        SupportInfo info = sample();
        info.logTail.clear();
        for (int n = 1; n <= 400; ++n) info.logTail << QString("line %1 ").arg(n) + QString(200, 'x');
        QUrl url = SupportReport::url(info);
        QVERIFY2(url.toEncoded().size() <= kMaxSupportUrlLength, qPrintable(QString::number(url.toEncoded().size())));
        QString body = decodedField(url, "body");
        // Everything that is not log stays; the log keeps its newest lines.
        QVERIFY(body.contains("Added nvinfer after"));
        QVERIFY(body.contains("Last validation: error"));
        QVERIFY(body.contains("line 400 "));
        QVERIFY(!body.contains("line 1 x"));
        // Even a huge action text and no log cannot break the limit.
        info.action = QString(50000, 'q');
        info.logTail.clear();
        QVERIFY(SupportReport::url(info).toEncoded().size() <= kMaxSupportUrlLength);
        // A short report is not cut at all.
        QVERIFY(decodedField(SupportReport::url(sample()), "body").contains("log line 1 from"));
    }

    void openingGoesThroughTheInterface() {
        Capture capture;
        QVERIFY(SupportReport::open(sample(), capture));
        QCOMPARE(capture.urls.size(), 1);
        QCOMPARE(decodedField(capture.urls[0], "title"), sample().title);
    }

    // ---- SlowWatch

    void askedAfterSixtySecondsAndEveryWaitAddsFifteen() {
        qint64 now = 1000;
        SlowWatch watch;
        watch.setClock([&now] { return now; });
        QSignalSpy slow(&watch, &SlowWatch::slow);
        watch.start();
        QCOMPARE(watch.elapsedMs(), qint64(0));
        now += SlowWatch::kFirstLimitMs - 1;
        watch.check();
        QCOMPARE(slow.count(), 0);
        QVERIFY(!watch.asking());
        now += 1;
        watch.check();
        QCOMPARE(slow.count(), 1);
        QVERIFY(watch.asking());
        now += 5000;
        watch.check();
        QCOMPARE(slow.count(), 1);  // asks once, not on every check

        watch.extend();  // Wait: the next question is kExtraMs later
        QVERIFY(!watch.asking());
        now = 1000 + SlowWatch::kFirstLimitMs + SlowWatch::kExtraMs - 1;
        watch.check();
        QCOMPARE(slow.count(), 1);
        now = 1000 + SlowWatch::kFirstLimitMs + SlowWatch::kExtraMs;
        watch.check();
        QCOMPARE(slow.count(), 2);
        watch.extend();
        now = 1000 + SlowWatch::kFirstLimitMs + 2 * SlowWatch::kExtraMs - 1;
        watch.check();
        QCOMPARE(slow.count(), 2);
        now = 1000 + SlowWatch::kFirstLimitMs + 2 * SlowWatch::kExtraMs;
        watch.check();
        QCOMPARE(slow.count(), 3);

        // A new validation starts from kFirstLimitMs again; a stopped watch asks nothing.
        watch.stop();
        now += 500000;
        watch.check();
        QCOMPARE(slow.count(), 3);
        watch.start();
        now += SlowWatch::kFirstLimitMs;
        watch.check();
        QCOMPARE(slow.count(), 4);
    }

    void waitWithoutAQuestionDoesNothing() {
        qint64 now = 0;
        SlowWatch watch;
        watch.setClock([&now] { return now; });
        QSignalSpy slow(&watch, &SlowWatch::slow);
        watch.start();
        watch.extend();  // nobody asked: no extra time is given
        now = SlowWatch::kFirstLimitMs;
        watch.check();
        QCOMPARE(slow.count(), 1);
    }
};

QTEST_GUILESS_MAIN(SupportTest)
#include "support_test.moc"
