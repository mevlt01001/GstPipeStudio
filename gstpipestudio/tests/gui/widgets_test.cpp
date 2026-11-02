// widgets_test.cpp – the widgets driven with QTest mouse and keyboard events (offscreen), on top of the fake helpers.
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QWheelEvent>

#include "canvasscene.h"
#include "canvasview.h"
#include "capsdialog.h"
#include "confirmdialog.h"
#include "elementitem.h"
#include "factorydelegate.h"
#include "flows.h"
#include "linkitem.h"
#include "logpanel.h"
#include "mainwindow.h"
#include "paditem.h"
#include "pickerdialog.h"
#include "propertydialog.h"
#include "sourcepanel.h"
#include "testsupport.h"
#include "theme.h"
#include "updatepanel.h"

using namespace support;

namespace {

template <class T>
T* visibleChild(QWidget* parent) {
    for (T* d : parent->findChildren<T*>()) {
        if (d->isVisible()) return d;
    }
    return nullptr;
}

template <class T>
T* shown(QWidget* parent) {
    T* found = nullptr;
    if (!QTest::qWaitFor([&] { return (found = visibleChild<T>(parent)) != nullptr; }, 8000)) return nullptr;
    return found;
}

void press(QDialog* dialog, const char* buttonName) {
    auto* button = dialog->findChild<QPushButton*>(buttonName);
    QVERIFY2(button, buttonName);
    QTest::mouseClick(button, Qt::LeftButton);
}

// A real double click is press, release, double-click event; QTest::mouseDClick sends only the last.
void doubleClick(QWidget* widget, const QPoint& at) {
    QTest::mouseClick(widget, Qt::LeftButton, Qt::NoModifier, at);
    QTest::mouseDClick(widget, Qt::LeftButton, Qt::NoModifier, at);
}

// Triggers an action of the open menu. A dialog opened by the action closes the popup, so look at the menu first.
void choose(QWidget* window, const char* action) {
    QPointer<QMenu> menu = visibleChild<QMenu>(window);
    QVERIFY2(menu, "no menu is open");
    QAction* a = menu->findChild<QAction*>(action);
    QVERIFY2(a, action);
    a->trigger();
    if (menu) menu->close();
}

QPoint viewPoint(CanvasView* view, const QPointF& scenePoint) { return view->mapFromScene(scenePoint); }

QString text(const std::string& s) { return QString::fromStdString(s); }

PropertyDialogInput inputFor(const ElementInfo* info) {
    PropertyDialogInput in;
    in.info = *info;
    in.title = "test";
    return in;
}

template <class W>
W* in(QDialog& dialog, const QString& objectName) {
    auto* row = dialog.findChild<QWidget*>(objectName);
    return row ? row->findChild<W*>() : nullptr;
}

}  // namespace

class WidgetsTest : public QObject {
    Q_OBJECT

    std::unique_ptr<Controller> c;

    // Source -> convert -> sink on a window that is shown and fitted.
    void buildChain(MainWindow& w, QString* src, QString* convert, QString* sink) {
        *src = addSource(*c, "videotestsrc");
        *convert = addAfter(*c, *src, "src", "videoconvert");
        *sink = addAfter(*c, *convert, "src", "fakesink");
        w.scene()->rebuild();
        w.view()->fitAll();
    }

private slots:
    void initTestCase() { Theme::apply(*qApp); }  // widgets are tested the way they look
    void init() { c = makeController(); }
    void cleanup() { c.reset(); }

    // ---- Theme

    void themeUsesTheColoursOfTheSpec() {
        QCOMPARE(Theme::background().name(), QString("#1b1b1b"));
        QCOMPARE(Theme::panel().name(), QString("#242424"));
        QCOMPARE(Theme::line().name(), QString("#3a3a3a"));
        QCOMPARE(Theme::text().name(), QString("#e8e8e8"));
        QCOMPARE(Theme::accent().name(), QString("#76b900"));
        QCOMPARE(Theme::accentHover().name(), QString("#8ed000"));
        QCOMPARE(Theme::error().name(), QString("#e5484d"));
        QCOMPARE(Theme::warning().name(), QString("#f5a524"));
        QVERIFY(Theme::styleSheet().contains("#76b900"));
        QVERIFY(!Theme::styleSheet().contains("@"));  // every token was replaced
        QCOMPARE(QApplication::font().family(), QString("DejaVu Sans"));
        QCOMPARE(QApplication::font().pointSize(), 10);
    }

    // ---- PropertyDialog

    void propertyDialogMakesTheRightEditor() {
        const ElementInfo* info = need(*c, "propdemo");
        PropertyDialog d(inputFor(info));
        QVERIFY(in<QCheckBox>(d, "editor_p-bool"));
        auto* spin = in<QSpinBox>(d, "editor_p-int");
        QVERIFY(spin);
        QCOMPARE(spin->minimum(), -10);
        QCOMPARE(spin->maximum(), 100);
        QCOMPARE(spin->value(), 5);
        auto* uspin = in<QDoubleSpinBox>(d, "editor_p-uint");
        QVERIFY(uspin);
        QCOMPARE(uspin->decimals(), 0);
        QCOMPARE(uspin->maximum(), 4294967295.0);
        auto* big = in<QLineEdit>(d, "editor_p-int64");
        QVERIFY(big && big->validator());
        QVERIFY(in<QLineEdit>(d, "editor_p-uint64")->validator());
        auto* real = in<QDoubleSpinBox>(d, "editor_p-float");
        QVERIFY(real);
        QCOMPARE(real->maximum(), 10.0);
        QCOMPARE(real->value(), 0.5);
        auto* combo = in<QComboBox>(d, "editor_p-enum");
        QVERIFY(combo);
        QCOMPARE(combo->count(), 3);
        QCOMPARE(combo->currentData().toString(), QString("one"));
        QWidget* flags = d.findChild<QWidget*>("editor_p-flags");
        QCOMPARE(flags->findChildren<QCheckBox*>().size(), 3);
        QVERIFY(flags->findChildren<QCheckBox*>()[0]->isChecked());   // a
        QVERIFY(!flags->findChildren<QCheckBox*>()[1]->isChecked());  // b
        QCOMPARE(in<QLineEdit>(d, "editor_p-caps")->text(), QString("ANY"));
        QVERIFY(in<QLineEdit>(d, "editor_p-fraction")->validator());
        QCOMPARE(in<QLineEdit>(d, "editor_p-string")->text(), QString("hello"));

        // Read-only, object-like and the Plan's own `name` are not offered; deprecated ones are dimmed.
        QVERIFY(!d.findChild<QWidget*>("row_p-readonly"));
        QVERIFY(!d.findChild<QWidget*>("row_p-object"));
        QVERIFY(!d.findChild<QWidget*>("row_name"));
        QVERIFY(!d.findChild<QWidget*>("row_parent"));
        QWidget* old = d.findChild<QWidget*>("row_p-old");
        QVERIFY(old);
        QVERIFY(old->findChild<QLabel*>()->text().contains("(eski)"));
        QVERIFY(old->findChild<QLabel*>()->styleSheet().contains(Theme::muted().name()));
        QVERIFY(!d.findChild<QWidget*>("row_p-int")->findChild<QLabel*>()->text().contains("(eski)"));
    }

    void propertyDialogReturnsOnlyWhatChanged() {
        const ElementInfo* info = need(*c, "propdemo");
        PropertyDialogInput input = inputFor(info);
        input.props = {{"fake-unknown", "keep-me"}};
        PropertyDialog d(input);
        d.show();
        QVERIFY(d.edit().props.size() == 1);  // untouched: only the entry no editor knows

        in<QSpinBox>(d, "editor_p-int")->setValue(42);
        in<QCheckBox>(d, "editor_p-bool")->click();
        in<QComboBox>(d, "editor_p-enum")->setCurrentIndex(1);
        d.findChild<QWidget*>("editor_p-flags")->findChildren<QCheckBox*>()[1]->click();
        in<QDoubleSpinBox>(d, "editor_p-double")->setValue(2.25);
        auto* text = in<QLineEdit>(d, "editor_p-string");
        text->selectAll();
        QTest::keyClicks(text, "world");
        for (const char* name : {"editor_p-uint64", "editor_p-int64"}) in<QLineEdit>(d, name)->clear();
        QTest::keyClicks(in<QLineEdit>(d, "editor_p-uint64"), "18446744073709551615");
        QTest::keyClicks(in<QLineEdit>(d, "editor_p-int64"), "-5");
        QTest::keyClicks(in<QLineEdit>(d, "editor_p-fraction"), "x");  // rejected by the validator
        d.findChild<QCheckBox*>("expose_p-int")->click();
        press(&d, "saveButton");
        QCOMPARE(d.result(), int(QDialog::Accepted));

        ElementEdit e = d.edit();
        QCOMPARE(e.props.at("p-int"), std::string("42"));
        QCOMPARE(e.props.at("p-bool"), std::string("true"));
        QCOMPARE(e.props.at("p-enum"), std::string("two"));
        QCOMPARE(e.props.at("p-flags"), std::string("a+b"));
        QCOMPARE(e.props.at("p-double"), std::string("2.25"));
        QCOMPARE(e.props.at("p-string"), std::string("world"));
        QCOMPARE(e.props.at("p-uint64"), std::string("18446744073709551615"));
        QCOMPARE(e.props.at("p-int64"), std::string("-5"));
        QCOMPARE(e.props.at("fake-unknown"), std::string("keep-me"));
        QVERIFY(e.props.find("p-fraction") == e.props.end());  // the "x" never got in, nothing changed
        QVERIFY(e.props.find("p-uint") == e.props.end());
        QCOMPARE(e.exposed, std::vector<std::string>{"p-int"});

        // Putting a value back to its default removes it from the plan.
        in<QSpinBox>(d, "editor_p-int")->setValue(5);
        gps::Props back = d.edit().props;
        QVERIFY(back.find("p-int") == back.end());
    }

    void propertyDialogStartsFromThePlansValues() {
        const ElementInfo* info = need(*c, "propdemo");
        PropertyDialogInput input = inputFor(info);
        input.props = {{"p-int", "77"}, {"p-enum", "3"}, {"p-flags", "b+c"}, {"p-bool", "true"}};
        input.exposed = {"p-string"};
        PropertyDialog d(input);
        QCOMPARE(in<QSpinBox>(d, "editor_p-int")->value(), 77);
        QCOMPARE(in<QComboBox>(d, "editor_p-enum")->currentData().toString(), QString("three"));  // number -> nick
        QVERIFY(in<QCheckBox>(d, "editor_p-bool")->isChecked());
        auto boxes = d.findChild<QWidget*>("editor_p-flags")->findChildren<QCheckBox*>();
        QVERIFY(!boxes[0]->isChecked() && boxes[1]->isChecked() && boxes[2]->isChecked());
        QVERIFY(d.findChild<QCheckBox*>("expose_p-string")->isChecked());
        QVERIFY(!d.findChild<QCheckBox*>("expose_p-int")->isChecked());
        QCOMPARE(d.edit().props.at("p-flags"), std::string("b+c"));  // untouched values are kept as written
        QCOMPARE(d.edit().exposed, std::vector<std::string>{"p-string"});
    }

    void propertyDialogSearchHidesRows() {
        PropertyDialog d(inputFor(need(*c, "propdemo")));
        d.show();
        auto* search = d.findChild<QLineEdit*>("searchBox");
        QTest::keyClicks(search, "frac");
        QVERIFY(d.findChild<QWidget*>("row_p-fraction")->isVisible());
        QVERIFY(!d.findChild<QWidget*>("row_p-int")->isVisible());
        search->clear();
        QVERIFY(d.findChild<QWidget*>("row_p-int")->isVisible());
        QTest::keyClicks(search, "nothing-like-this");
        QVERIFY(!d.findChild<QWidget*>("row_p-fraction")->isVisible());
    }

    void propertyDialogCancelChangesNothing() {
        PropertyDialog d(inputFor(need(*c, "propdemo")));
        d.show();
        in<QSpinBox>(d, "editor_p-int")->setValue(50);
        press(&d, "cancelButton");
        QCOMPARE(d.result(), int(QDialog::Rejected));
        QVERIFY(!d.isVisible());
    }

    void propertyDialogRequestSrcPads() {
        const ElementInfo* tee = need(*c, "tee");
        PropertyDialogInput input = inputFor(tee);
        input.requestSrc = gps::defaultRequestSrc({{"src_%u", "request"}});  // a new tee: one pad
        PropertyDialog fresh(input);
        QCOMPARE(fresh.findChild<QSpinBox*>("padCount_src_%u")->value(), 1);
        QCOMPARE(fresh.findChild<QSpinBox*>("padCount_src_%u")->minimum(), 0);

        input.requestSrc = {{"src_%u", 3}};
        input.linkedSrc = {{"src_%u", 2}};
        PropertyDialog d(input);
        auto* pads = d.findChild<QSpinBox*>("padCount_src_%u");
        QCOMPARE(pads->value(), 3);
        QCOMPARE(pads->minimum(), 2);  // cannot go below the linked pads
        pads->setValue(4);
        QCOMPARE(d.edit().requestSrc.at("src_%u"), 4);

        // An element with only Always pads has no such section.
        PropertyDialog plain(inputFor(need(*c, "queue")));
        QVERIFY(plain.findChildren<QSpinBox*>().isEmpty());
    }

    // ---- CapsDialog

    void capsTextRoundTrips() {
        for (const char* t : {"video/x-raw", "video/x-raw, width=(int)640",
                              "video/x-raw(memory:NVMM), format=(string)NV12, framerate=(fraction)30/1",
                              "audio/x-raw, layout=(string)\"a b, c\", rate=(gint64)44100, ok=(boolean)true"}) {
            QString original = QString::fromUtf8(t);
            QCOMPARE(QString::fromStdString(parseCapsChoice(original).toText()), original);
        }
        QVERIFY(parseCapsChoice("").media.empty());
        QCOMPARE(parseCapsChoice("video/x-raw(memory:NVMM, memory:DMABuf)").features.size(), size_t(2));
    }

    void capsDialogBuildsTheChoice() {
        CapsDialogInput input;
        input.title = "Caps düzenle";
        const ElementInfo* src = need(*c, "videotestsrc");
        input.allowed = src->padTemplate("src", "src")->structures;
        CapsDialog d(input);
        d.show();
        QCOMPARE(d.caps(), QString());  // Otomatik
        auto* media = d.findChild<QComboBox*>("mediaCombo");
        QCOMPARE(media->count(), 2);
        QCOMPARE(media->itemText(1), QString("video/x-raw"));
        media->setCurrentIndex(1);
        QCOMPARE(d.caps(), QString("video/x-raw"));  // every field still Otomatik

        // list -> combo box with Otomatik first
        auto* format = d.findChild<QWidget*>("field_format")->findChild<QComboBox*>();
        QCOMPARE(format->itemText(0), QString("Otomatik"));
        QCOMPARE(format->count(), 4);
        format->setCurrentIndex(2);
        QCOMPARE(d.caps(), QString("video/x-raw, format=(string)NV12"));

        // range -> number box, active once "Sabitle" is ticked
        QWidget* width = d.findChild<QWidget*>("field_width");
        auto* pin = width->findChild<QCheckBox*>();
        auto* number = width->findChild<QSpinBox*>();
        QVERIFY(!number->isEnabled());
        pin->click();
        QVERIFY(number->isEnabled());
        number->setValue(640);
        QCOMPARE(d.caps(), QString("video/x-raw, format=(string)NV12, width=(int)640"));
        auto* rate = d.findChild<QWidget*>("field_framerate");
        rate->findChild<QCheckBox*>()->click();
        rate->findChild<QLineEdit*>()->setText("15/1");
        QCOMPARE(d.caps(), QString("video/x-raw, format=(string)NV12, width=(int)640, framerate=(fraction)15/1"));
        pin->click();  // unticked: Otomatik again
        QCOMPARE(d.caps(), QString("video/x-raw, format=(string)NV12, framerate=(fraction)15/1"));
        media->setCurrentIndex(0);
        QCOMPARE(d.caps(), QString());
    }

    void capsDialogFixedFieldsAndFeatures() {
        const ElementInfo* dec = need(*c, "jpegdec");
        CapsDialogInput input;
        input.allowed = dec->padTemplate("sink", "sink")->structures;
        CapsDialog d(input);
        d.findChild<QComboBox*>("mediaCombo")->setCurrentIndex(1);
        QWidget* parsed = d.findChild<QWidget*>("field_parsed");
        QVERIFY(parsed->findChild<QLabel*>());  // fixed: shown as a label
        QCOMPARE(parsed->findChild<QLabel*>()->text(), QString("true"));
        parsed->findChild<QCheckBox*>()->click();
        QCOMPARE(d.caps(), QString("image/jpeg, parsed=(boolean)true"));

        const ElementInfo* nv = need(*c, "nvvideoconvert");
        CapsDialogInput nvInput;
        nvInput.allowed = nv->padTemplate("src", "src")->structures;
        CapsDialog n(nvInput);
        auto* media = n.findChild<QComboBox*>("mediaCombo");
        QCOMPARE(media->itemText(1), QString("video/x-raw(memory:NVMM)"));
        media->setCurrentIndex(1);
        QCOMPARE(n.caps(), QString("video/x-raw(memory:NVMM)"));
    }

    void capsDialogPadSettings() {
        const ElementInfo* mixer = need(*c, "compositor");
        CapsDialogInput input;
        input.allowed = mixer->padTemplate("src", "src")->structures;
        input.padProperties = mixer->padTemplate("sink_%u", "sink")->padProperties;
        input.padProps = {{"xpos", "100"}};
        CapsDialog d(input);
        d.show();
        QVERIFY(d.findChild<QWidget*>("padSettings")->isVisible());
        QCOMPARE(d.findChild<QWidget*>("padEditor_xpos")->findChild<QSpinBox*>()->value(), 100);
        d.findChild<QWidget*>("padEditor_ypos")->findChild<QSpinBox*>()->setValue(-20);
        QCOMPARE(d.padProps().at("xpos"), std::string("100"));
        QCOMPARE(d.padProps().at("ypos"), std::string("-20"));

        // No pad properties: the section is not there.
        CapsDialogInput plain;
        plain.allowed = input.allowed;
        CapsDialog p(plain);
        p.show();
        QVERIFY(!p.findChild<QWidget*>("padSettings")->isVisible());
    }

    void capsDialogPrefillsAndHandlesNothingKnown() {
        const ElementInfo* src = need(*c, "videotestsrc");
        CapsDialogInput input;
        input.allowed = src->padTemplate("src", "src")->structures;
        input.current = "video/x-raw, format=(string)RGBA, width=(int)800";
        CapsDialog d(input);
        QCOMPARE(d.findChild<QComboBox*>("mediaCombo")->currentIndex(), 1);
        QCOMPARE(d.caps(), QString("video/x-raw, format=(string)RGBA, width=(int)800"));

        CapsDialogInput empty;  // an update gave no structures
        CapsDialog e(empty);
        e.show();
        QCOMPARE(e.findChild<QComboBox*>("mediaCombo")->count(), 1);
        QCOMPARE(e.caps(), QString());
        press(&e, "saveButton");
        QCOMPARE(e.result(), int(QDialog::Accepted));
    }

    // ---- PickerDialog and ConfirmDialog

    void pickerRanksAndSearches() {
        PickerData data;
        data.title = "Seçici";
        data.existing = {{"mix_1", "compositor", "sink_%u", true}, {"over_2", "textoverlay", "video_sink", false}};
        data.compatible = {"jpegdec", "videoconvert"};
        for (const ListEntry& e : c->catalog()) {
            if (!e.isSource) data.candidates << e;
        }
        PickerDialog d(data);
        d.show();
        auto* list = d.findChild<QListWidget*>("pickerList");
        auto rows = [&] {
            QStringList out;
            for (int i = 0; i < list->count(); ++i) {
                if (!list->item(i)->isHidden()) out << list->item(i)->text();
            }
            return out;
        };
        QStringList all = rows();
        QCOMPARE(all[0], QString("Var olan elementler"));
        QCOMPARE(all[1], QString("mix_1 · sink_%u"));
        QCOMPARE(list->item(1)->data(HintRole).toString(), QString("Request a pad"));
        QCOMPARE(all[3], QString("Yeni element"));
        QCOMPARE(all[4], QString("jpegdec"));       // compatible first, in the helper's order
        QCOMPARE(all[5], QString("videoconvert"));
        QCOMPARE(list->item(4)->data(HintRole).toString(), QString("uyumlu"));
        QVERIFY(list->item(6)->data(HintRole).toString().isEmpty());
        QVERIFY(!all.contains("videotestsrc"));     // sources are never offered behind a pad
        // the rest by rank (256 first), then name
        QCOMPARE(all[6], QString("compositor"));
        QVERIFY(list->currentItem() == list->item(1));

        auto* search = d.findChild<QLineEdit*>("searchBox");
        QTest::keyClicks(search, "tee");
        QCOMPARE(rows(), QStringList({"Yeni element", "tee"}));  // the empty section header goes too
        QCOMPARE(d.choice().kind, PickerChoice::NewElement);
        QCOMPARE(d.choice().factory, QString("tee"));
        search->clear();
        QTest::keyClicks(search, "request");
        QCOMPARE(rows(), QStringList({"Var olan elementler", "mix_1 · sink_%u"}));
        QCOMPARE(d.choice().kind, PickerChoice::Existing);
        QCOMPARE(d.choice().existing.element, QString("mix_1"));
        QVERIFY(d.choice().existing.request);
        search->clear();
        QTest::keyClicks(search, "zzzz");
        QVERIFY(rows().isEmpty());
        QCOMPARE(d.choice().kind, PickerChoice::None);

        // Enter takes the best hit; Down moves on.
        search->clear();
        QTest::keyClicks(search, "con");
        QTest::keyClick(search, Qt::Key_Down);
        QCOMPARE(d.choice().factory, QString("nvvideoconvert"));
        QTest::keyClick(search, Qt::Key_Up);
        QCOMPARE(d.choice().factory, QString("videoconvert"));
        QTest::keyClick(search, Qt::Key_Return);
        QCOMPARE(d.result(), int(QDialog::Accepted));
    }

    void pickerDoubleClickAndCancel() {
        PickerData data;
        data.compatible = {"queue"};
        for (const ListEntry& e : c->catalog()) {
            if (!e.isSource) data.candidates << e;
        }
        PickerDialog d(data);
        d.show();
        press(&d, "cancelButton");
        QCOMPARE(d.result(), int(QDialog::Rejected));

        PickerDialog second(data);
        second.show();
        auto* list = second.findChild<QListWidget*>("pickerList");
        QListWidgetItem* item = list->item(1);
        doubleClick(list->viewport(), list->visualItemRect(item).center());
        QCOMPARE(second.result(), int(QDialog::Accepted));
        QCOMPARE(second.choice().factory, QString("queue"));
    }

    void confirmDialogDefaultsToTheSafeAnswer() {
        ConfirmDialog d("Silinecekler", "Şu 2 element silinecek", {"a (x)", "b (y)"}, "Sil");
        d.show();
        QCOMPARE(d.findChild<QListWidget*>("removalList")->count(), 2);
        QVERIFY(d.findChild<QPushButton*>("cancelButton")->isDefault());
        QTest::keyClick(&d, Qt::Key_Return);
        QCOMPARE(d.result(), int(QDialog::Rejected));
        ConfirmDialog e("Silinecekler", "x", {"a"}, "Sil");
        e.show();
        press(&e, "saveButton");
        QCOMPARE(e.result(), int(QDialog::Accepted));
    }

    // ---- canvas

    void canvasDrawsElementsPadsAndLinks() {
        MainWindow w(c.get());
        w.show();
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);
        CanvasScene* scene = w.scene();
        QVERIFY(scene->element(src) && scene->element(convert) && scene->element(sink));
        QCOMPARE(scene->links().size(), 2);
        QCOMPARE(scene->element(convert)->pos(), QPointF(c->plan().find(convert.toStdString())->x, c->plan().find(convert.toStdString())->y));
        QCOMPARE(scene->pad(src, "src", true)->view().state, PadView::Linked);
        QCOMPARE(scene->pad(convert, "sink", false)->view().state, PadView::Linked);
        QVERIFY(!scene->pad(sink, "src", true));

        // Links run from the edge of one pad to the edge of the other.
        LinkItem* link = scene->links()[0];
        QVERIFY(link->shape().contains(link->mapFromScene(scene->pad(src, "src", true)->center() + QPointF(9, 0))));
        QVERIFY(link->shape().contains(link->mapFromScene(scene->pad(convert, "sink", false)->center() - QPointF(9, 0))));
        QVERIFY(!link->shape().contains(link->mapFromScene(scene->pad(src, "src", true)->center() - QPointF(9, 0))));

        // A caps choice gives the link a badge.
        QVERIFY(c->setCaps(src, "src", "video/x-raw, width=(int)640", {}));
        QVERIFY(settle(*c));
        scene->rebuild();
        QCOMPARE(scene->links()[0]->view().badge, QString("caps · 1"));
        QCOMPARE(scene->links()[0]->toolTip(), QString("video/x-raw, width=(int)640"));
        QVERIFY(scene->links()[0]->shape().contains(scene->links()[0]->midPoint() + QPointF(0, 8)));  // the badge is part of the link
    }

    void canvasShowsRequestSinkPadsOnlyWhenLinked() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        QString mix = addAfter(*c, src, "src", "compositor", "sink_%u");
        scene_rebuild(w);
        QVERIFY(w.scene()->pad(mix, "sink_%u#0", false));
        QVERIFY(!w.scene()->pad(mix, "sink_%u#1", false));
        QString second = addSource(*c, "videotestsrc");
        need(*c, "compositor");
        QVERIFY(c->linkExisting(second, "src", mix, "sink_%u"));
        QVERIFY(settle(*c));
        scene_rebuild(w);
        QVERIFY(w.scene()->pad(mix, "sink_%u#1", false));
        QCOMPARE(w.scene()->links().size(), 2);
        QCOMPARE(w.scene()->links()[1]->view().sinkIdentity, QString("sink_%u#1"));
    }

    void canvasPadColoursFollowTheSpec() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        QString mix = addAfter(*c, src, "src", "compositor", "sink_%u");
        QString raw = addSource(*c, "filesrc", withProps({{"location", "/data/random.bin"}}));
        scene_rebuild(w);

        CanvasScene* scene = w.scene();
        QRectF area = scene->itemsBoundingRect().adjusted(-20, -20, 20, 20);
        QImage image(area.size().toSize(), QImage::Format_ARGB32);
        image.fill(Qt::black);
        QPainter painter(&image);
        scene->render(&painter, QRectF(QPointF(0, 0), area.size()), area);
        painter.end();
        auto at = [&](PadItem* pad, double dx) {
            QPointF p = pad->center() - area.topLeft() + QPointF(dx, 0);
            return image.pixelColor(p.toPoint());
        };
        auto near = [](const QColor& a, const QColor& b) {
            return qAbs(a.red() - b.red()) < 24 && qAbs(a.green() - b.green()) < 24 && qAbs(a.blue() - b.blue()) < 24;
        };
        PadItem* linkedSrc = scene->pad(src, "src", true);
        PadItem* freeSrc = scene->pad(mix, "src", true);
        PadItem* unknownSrc = scene->pad(raw, "src", true);
        PadItem* linkedSink = scene->pad(mix, "sink_%u#0", false);
        QVERIFY(linkedSrc && freeSrc && unknownSrc && linkedSink);
        QCOMPARE(freeSrc->view().state, PadView::Free);
        QCOMPARE(unknownSrc->view().state, PadView::Unknown);

        QVERIFY2(near(at(linkedSrc, 0), Theme::accent()), "linked src: filled green");
        QVERIFY2(near(at(freeSrc, 6), Theme::accent()), "free src: green ring");
        QVERIFY2(near(at(freeSrc, 0), Theme::background()), "free src: hollow");
        QVERIFY2(near(at(unknownSrc, 6), Theme::warning()), "unknown data: orange ring");
        QVERIFY2(near(at(linkedSink, 0), Theme::muted()), "linked sink: filled gray");
    }

    void canvasErrorShowsRedOnTheElement() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        QString sink = addAfter(*c, src, "src", "fakesink", "sink", withProps({{"fake-error", "no way"}}));
        scene_rebuild(w);
        QCOMPARE(w.scene()->element(sink)->view().note, QString("no way"));
        QVERIFY(w.scene()->element(sink)->view().isError);
        QVERIFY(w.scene()->element(src)->view().note.isEmpty());

        QRectF area = w.scene()->itemsBoundingRect().adjusted(-20, -20, 20, 20);
        QImage image(area.size().toSize(), QImage::Format_ARGB32);
        image.fill(Qt::black);
        QPainter painter(&image);
        w.scene()->render(&painter, QRectF(QPointF(0, 0), area.size()), area);
        painter.end();
        QPointF top = w.scene()->element(sink)->pos() + QPointF(120, 0) - area.topLeft();
        QColor border = image.pixelColor(top.toPoint());
        QVERIFY2(qAbs(border.red() - Theme::error().red()) < 30 && border.green() < 110, qPrintable(border.name()));
    }

    void canvasClicksReachTheRightSignals() {
        MainWindow w(c.get());
        w.show();
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);
        // The last element has no free pad, so make one: cut the tail off.
        QVERIFY(c->cut(convert, "src"));
        QVERIFY(settle(*c));
        w.scene()->rebuild();
        w.view()->fitAll();
        CanvasView* view = w.view();
        QWidget* port = view->viewport();
        // Queued connections would open dialogs: look at the scene's own signals instead.
        QSignalSpy freePad(w.scene(), &CanvasScene::freeSrcPadClicked);
        QSignalSpy linkMenu(w.scene(), &CanvasScene::linkMenuRequested);
        QSignalSpy elementMenu(w.scene(), &CanvasScene::elementMenuRequested);
        QSignalSpy activated(w.scene(), &CanvasScene::elementActivated);
        w.scene()->disconnect(&w);  // keep the window's slots out of this test

        // Left click on the free src pad of the convert element.
        QTest::mouseClick(port, Qt::LeftButton, Qt::NoModifier, viewPoint(view, w.scene()->pad(convert, "src", true)->center()));
        QCOMPARE(freePad.count(), 1);
        QCOMPARE(freePad[0][0].toString(), convert);
        QCOMPARE(freePad[0][1].toString(), QString("src"));
        // A left click on a linked pad does nothing (the menu is on the right button).
        QTest::mouseClick(port, Qt::LeftButton, Qt::NoModifier, viewPoint(view, w.scene()->pad(src, "src", true)->center()));
        QCOMPARE(freePad.count(), 1);
        QCOMPARE(linkMenu.count(), 0);

        // Right click on the linked src pad, on the sink pad it feeds, and on the link itself.
        QTest::mouseClick(port, Qt::RightButton, Qt::NoModifier, viewPoint(view, w.scene()->pad(src, "src", true)->center()));
        QCOMPARE(linkMenu.count(), 1);
        QCOMPARE(linkMenu[0][0].toString(), src);
        QCOMPARE(linkMenu[0][1].toString(), QString("src"));
        QTest::mouseClick(port, Qt::RightButton, Qt::NoModifier, viewPoint(view, w.scene()->pad(convert, "sink", false)->center()));
        QCOMPARE(linkMenu.count(), 2);
        QCOMPARE(linkMenu[1][0].toString(), src);
        LinkItem* link = w.scene()->links()[0];
        QTest::mouseClick(port, Qt::RightButton, Qt::NoModifier, viewPoint(view, link->mapToScene(link->midPoint())));
        QCOMPARE(linkMenu.count(), 3);
        QCOMPARE(linkMenu[2][0].toString(), src);
        // A right click on a free pad is not a link menu.
        QTest::mouseClick(port, Qt::RightButton, Qt::NoModifier, viewPoint(view, w.scene()->pad(convert, "src", true)->center()));
        QCOMPARE(linkMenu.count(), 3);

        // Right click and double click on an element body.
        QPointF body = w.scene()->element(convert)->pos() + QPointF(120, 12);
        QTest::mouseClick(port, Qt::RightButton, Qt::NoModifier, viewPoint(view, body));
        QCOMPARE(elementMenu.count(), 1);
        QCOMPARE(elementMenu[0][0].toString(), convert);
        QTest::mouseDClick(port, Qt::LeftButton, Qt::NoModifier, viewPoint(view, body));
        QVERIFY(activated.count() >= 1);
        QCOMPARE(activated[0][0].toString(), convert);
    }

    void canvasDragSavesThePosition() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        w.scene()->rebuild();
        w.view()->resetTransform();
        w.view()->centerOn(w.scene()->element(src));
        QPointF before = w.scene()->element(src)->pos();
        QPoint from = viewPoint(w.view(), before + QPointF(100, 12));
        QTest::mousePress(w.view()->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QMouseEvent move(QEvent::MouseMove, QPointF(from + QPoint(60, 90)), w.view()->viewport()->mapToGlobal(QPointF(from + QPoint(60, 90))),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(w.view()->viewport(), &move);
        QTest::mouseRelease(w.view()->viewport(), Qt::LeftButton, Qt::NoModifier, from + QPoint(60, 90));
        QVERIFY(waitFor([&] { return c->plan().find(src.toStdString())->x != before.x(); }, 3000));
        QCOMPARE(c->plan().find(src.toStdString())->x, before.x() + 60);
        QCOMPARE(c->plan().find(src.toStdString())->y, before.y() + 90);
        QVERIFY(c->dirty());
    }

    void canvasZoomAndPan() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        w.scene()->rebuild();
        CanvasView* view = w.view();
        view->resetTransform();
        double start = view->zoom();
        QWheelEvent in(QPointF(100, 100), QPointF(100, 100), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(view->viewport(), &in);
        QVERIFY(view->zoom() > start);
        QWheelEvent out(QPointF(100, 100), QPointF(100, 100), QPoint(), QPoint(0, -240), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(view->viewport(), &out);
        QVERIFY(view->zoom() < start);
        for (int i = 0; i < 40; ++i) view->zoomBy(1.5);
        QCOMPARE(view->zoom(), 3.0);
        for (int i = 0; i < 80; ++i) view->zoomBy(0.5);
        QCOMPARE(view->zoom(), 0.2);

        // Pan: drag on the empty background.
        view->resetTransform();
        view->zoomBy(2.0);
        view->centerOn(w.scene()->element(src));
        int before = view->horizontalScrollBar()->value();
        QPoint empty(view->viewport()->width() - 40, view->viewport()->height() - 40);
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, empty);
        QMouseEvent move(QEvent::MouseMove, QPointF(empty - QPoint(80, 0)), view->viewport()->mapToGlobal(QPointF(empty - QPoint(80, 0))),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &move);
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, empty - QPoint(80, 0));
        QVERIFY(view->horizontalScrollBar()->value() != before);

        view->fitAll();
        QVERIFY(view->zoom() <= 1.0);
    }

    // ---- main window and the dialog flows

    void actionsFollowTheStateOfTheUpdate() {
        MainWindow w(c.get());
        w.show();
        auto act = [&](const char* name) { return w.findChild<QAction*>(name); };
        auto label = [&] { return w.findChild<QLabel*>("stateLabel")->text(); };
        // empty canvas
        QVERIFY(act("actionNew")->isEnabled() && act("actionOpen")->isEnabled() && act("actionAddSource")->isEnabled());
        QVERIFY(!act("actionSave")->isEnabled() && !act("actionRun")->isEnabled() && !act("actionUpdate")->isEnabled());
        QVERIFY(label().contains("Boş tuval"));

        // updating: everything that edits is off
        need(*c, "videotestsrc");
        QVERIFY(c->addSource("videotestsrc", {}));
        QVERIFY(!act("actionNew")->isEnabled() && !act("actionOpen")->isEnabled() && !act("actionAddSource")->isEnabled());
        QVERIFY(!act("actionSave")->isEnabled() && !act("actionRun")->isEnabled());
        QCOMPARE(label(), QString("Güncelleniyor…"));
        QVERIFY(w.findChild<QProgressBar*>() != nullptr);
        QVERIFY(settle(*c));

        // ok but incomplete: Save yes, Run no
        QVERIFY(act("actionSave")->isEnabled());
        QVERIFY(act("actionSaveAs")->isEnabled());
        QVERIFY(!act("actionRun")->isEnabled());
        QVERIFY(label().contains("tamamlanmadı"));

        // complete: both
        QString src = QString::fromStdString(c->plan().elements[0].name);
        QString sink = addAfter(*c, src, "src", "fakesink");
        QVERIFY(act("actionRun")->isEnabled());
        QCOMPARE(label(), QString("Pipeline tamamlandı"));

        // failed: both off, red text
        QVERIFY(c->editElement(sink, withProps({{"fake-error", "bad"}})));
        QVERIFY(settle(*c));
        QVERIFY(!act("actionSave")->isEnabled() && !act("actionSaveAs")->isEnabled() && !act("actionRun")->isEnabled());
        QVERIFY(act("actionUpdate")->isEnabled());  // retrying is allowed
        QVERIFY(label().contains("başarısız"));
        QVERIFY(w.findChild<QLabel*>("stateLabel")->styleSheet().contains(Theme::error().name()));
        QVERIFY(w.windowTitle().endsWith("*"));
    }

    void statusBarShowsTheLaunchCommandAfterSaving() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        QString sink = addAfter(*c, src, "src", "fakesink");
        QCOMPARE(w.findChild<QLabel*>("commandLabel")->text(), QString());
        QTemporaryDir dir;
        QVERIFY(w.saveFile(dir.filePath("demo.conf")));
        QString shown = w.findChild<QLabel*>("commandLabel")->text();
        QVERIFY(shown.contains("gst-launch-1.0 demo"));
        QVERIFY(shown.contains("GPS_PIPELINE_PATH="));
        QVERIFY(w.windowTitle().contains("demo.conf"));
        QVERIFY(!w.windowTitle().endsWith("*"));
        QVERIFY(w.saveFile(QString()));  // Kaydet again, same file
    }

    void updateAndLogPanelsShowTheHelpersAnswer() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        QString sink = addAfter(*c, src, "src", "fakesink", "sink", withProps({{"fake-error", "kaput"}}));
        UpdatePanel* panel = w.updatePanel();
        QVERIFY(panel->findChild<QLabel*>("updateStatus")->text().startsWith("Hata"));
        QVERIFY(panel->findChild<QLabel*>("updateMessage")->text().contains(sink));  // the element is named
        QVERIFY(panel->findChild<QLabel*>("updateMessage")->text().contains("kaput"));
        QVERIFY(panel->findChild<QLabel*>("updateStatus")->styleSheet().contains(Theme::error().name()));

        QVERIFY(c->editElement(sink, {}));
        QVERIFY(settle(*c));
        QVERIFY(panel->findChild<QLabel*>("updateStatus")->text().startsWith("Tamam, pipeline tamamlandı"));
        QCOMPARE(panel->table()->rowCount(), c->result().pads.size());
        QCOMPARE(panel->table()->rowCount(), 2);  // the src pad and the sink pad
        QCOMPARE(panel->table()->item(0, 0)->text(), src);

        QString log = w.logPanel()->toPlainText();
        QVERIFY(log.contains("[gps-update] fake gps-update: 2 elements, 1 links"));
        QVERIFY(log.contains("Güncelleme: Hata"));
        QVERIFY(log.contains("kaput"));
        QVERIFY(w.logPanel()->isReadOnly());
        QVERIFY(w.logPanel()->font().styleHint() == QFont::Monospace || w.logPanel()->font().family().contains("Mono"));
        QCOMPARE(w.bottomTabs()->tabText(0), QString("Günlük"));
        QCOMPARE(w.bottomTabs()->tabText(1), QString("Güncelleme"));
    }

    void refusedAndUnknownShowInTheUpdatePanel() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        addAfter(*c, src, "src", "jpegdec");
        QVERIFY(w.updatePanel()->findChild<QLabel*>("updateLists")->text().contains("Reddedilen bağlantılar"));
        QVERIFY(w.updatePanel()->findChild<QLabel*>("updateStatus")->styleSheet().contains(Theme::warning().name()));
    }

    void sourcePanelListsSourcesWithTheNvidiaBadge() {
        MainWindow w(c.get());
        w.show();
        auto* list = w.sourcePanel()->findChild<QListWidget*>("sourceList");
        QStringList names;
        QMap<QString, bool> nvidia;
        for (int i = 0; i < list->count(); ++i) {
            names << list->item(i)->text();
            nvidia[list->item(i)->text()] = list->item(i)->data(NvidiaRole).toBool();
        }
        QVERIFY(names.contains("videotestsrc") && names.contains("filesrc") && names.contains("audiotestsrc"));
        QVERIFY(!names.contains("queue") && !names.contains("fakesink"));  // only sources
        QVERIFY(nvidia["nvarguscamerasrc"]);
        QVERIFY(!nvidia["videotestsrc"]);

        auto* search = w.sourcePanel()->findChild<QLineEdit*>("sourceSearch");
        QTest::keyClicks(search, "argus");
        int visible = 0;
        for (int i = 0; i < list->count(); ++i) visible += !list->item(i)->isHidden();
        QCOMPARE(visible, 1);
    }

    void addSourceFromThePanelAndCancel() {
        MainWindow w(c.get());
        w.show();
        auto* list = w.sourcePanel()->findChild<QListWidget*>("sourceList");
        QListWidgetItem* item = nullptr;
        for (int i = 0; i < list->count(); ++i) {
            if (list->item(i)->text() == "videotestsrc") item = list->item(i);
        }
        QVERIFY(item);
        // Cancel: nothing happens.
        doubleClick(list->viewport(), list->visualItemRect(item).center());
        auto* dialog = shown<PropertyDialog>(&w);
        QVERIFY(dialog);
        QCOMPARE(dialog->findChildren<QLineEdit*>("searchBox").size(), 1);
        press(dialog, "cancelButton");
        QTest::qWait(100);
        QVERIFY(c->plan().empty());
        QVERIFY(!c->dirty());

        // Save: the element is in the plan and an update runs.
        doubleClick(list->viewport(), list->visualItemRect(item).center());
        dialog = shown<PropertyDialog>(&w);
        QVERIFY(dialog);
        in<QSpinBox>(*dialog, "editor_num-buffers")->setValue(25);
        press(dialog, "saveButton");
        QVERIFY(waitFor([&] { return c->plan().elements.size() == 1 && !c->busy(); }, 8000));
        QCOMPARE(c->plan().elements[0].props.at("num-buffers"), std::string("25"));
        QVERIFY(c->result().ok());
        QVERIFY(waitFor([&] { return w.scene()->element("videotestsrc_0") != nullptr; }));
    }

    void toolbarAddSourceUsesThePicker() {
        MainWindow w(c.get());
        w.show();
        w.findChild<QAction*>("actionAddSource")->trigger();
        auto* picker = shown<PickerDialog>(&w);
        QVERIFY(picker);
        auto* list = picker->findChild<QListWidget*>("pickerList");
        QCOMPARE(list->item(0)->text(), QString("Kaynaklar"));
        QTest::keyClicks(picker->findChild<QLineEdit*>("searchBox"), "audiot");
        QTest::keyClick(picker->findChild<QLineEdit*>("searchBox"), Qt::Key_Return);
        auto* dialog = shown<PropertyDialog>(&w);
        QVERIFY(dialog);
        press(dialog, "saveButton");
        QVERIFY(waitFor([&] { return c->plan().elements.size() == 1 && !c->busy(); }, 8000));
        QCOMPARE(c->plan().elements[0].factory, std::string("audiotestsrc"));
    }

    void addElementBehindAPadWithEveryCancelPoint() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        w.scene()->rebuild();
        w.view()->fitAll();
        auto clickFreePad = [&](const QString& element) {
            w.scene()->rebuild();
            QTest::mouseClick(w.view()->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint(w.view(), w.scene()->pad(element, "src", true)->center()));
        };
        QString before = text(c->plan().toConf());

        // 1. Picker: Cancel.
        clickFreePad(src);
        auto* picker = shown<PickerDialog>(&w);
        QVERIFY(picker);
        QCOMPARE(picker->findChild<QListWidget*>("pickerList")->item(0)->text(), QString("Yeni element"));  // nothing existing yet
        press(picker, "cancelButton");
        QTest::qWait(50);
        QCOMPARE(text(c->plan().toConf()), before);

        // 2. PropertyDialog: Cancel.
        clickFreePad(src);
        picker = shown<PickerDialog>(&w);
        QTest::keyClicks(picker->findChild<QLineEdit*>("searchBox"), "videoconvert");
        press(picker, "saveButton");
        auto* props = shown<PropertyDialog>(&w);
        QVERIFY(props);
        QVERIFY(props->windowTitle().contains("videoconvert"));
        press(props, "cancelButton");
        QTest::qWait(50);
        QCOMPARE(text(c->plan().toConf()), before);

        // 3. Save, then the CapsDialog: Cancel leaves the element.
        clickFreePad(src);
        picker = shown<PickerDialog>(&w);
        QTest::keyClicks(picker->findChild<QLineEdit*>("searchBox"), "videoconvert");
        press(picker, "saveButton");
        props = shown<PropertyDialog>(&w);
        press(props, "saveButton");
        auto* caps = shown<CapsDialog>(&w);
        QVERIFY(caps);
        QVERIFY(!c->busy());
        QCOMPARE(c->plan().elements.size(), size_t(2));
        QCOMPARE(caps->findChild<QComboBox*>("mediaCombo")->itemText(1), QString("video/x-raw"));
        press(caps, "cancelButton");
        QTest::qWait(50);
        QCOMPARE(c->plan().elements.size(), size_t(2));
        QVERIFY(c->plan().links[0].caps.empty());
        QString convert = text(c->plan().elements[1].name);

        // 4. A second element, and this time the CapsDialog is saved with a choice.
        clickFreePad(convert);
        picker = shown<PickerDialog>(&w);
        QTest::keyClicks(picker->findChild<QLineEdit*>("searchBox"), "fakesink");
        QTest::keyClick(picker->findChild<QLineEdit*>("searchBox"), Qt::Key_Return);
        props = shown<PropertyDialog>(&w);
        press(props, "saveButton");
        caps = shown<CapsDialog>(&w);
        QVERIFY(caps);
        caps->findChild<QComboBox*>("mediaCombo")->setCurrentIndex(1);
        caps->findChild<QWidget*>("field_format")->findChild<QComboBox*>()->setCurrentIndex(2);  // NV12
        press(caps, "saveButton");
        QVERIFY(waitFor([&] { return !c->busy() && c->plan().links[1].caps == "video/x-raw, format=(string)NV12"; }, 8000));
        QVERIFY(c->result().isComplete());
        QVERIFY(c->canRun());
    }

    void severalSinkPadsAreAskedAfterThePropertyDialog() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        w.scene()->rebuild();
        w.view()->fitAll();
        auto start = [&] {
            w.scene()->rebuild();
            QTest::mouseClick(w.view()->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint(w.view(), w.scene()->pad(src, "src", true)->center()));
            auto* picker = shown<PickerDialog>(&w);
            QTest::keyClicks(picker->findChild<QLineEdit*>("searchBox"), "dualsink");
            press(picker, "saveButton");
            auto* props = shown<PropertyDialog>(&w);
            press(props, "saveButton");
            return shown<QInputDialog>(&w);
        };
        QString before = text(c->plan().toConf());

        // Cancel at the question: nothing changed.
        QInputDialog* question = start();
        QVERIFY(question);
        QCOMPARE(question->comboBoxItems(), QStringList({"main", "aux"}));
        QVERIFY(question->labelText().contains("sink pad"));
        question->reject();
        QTest::qWait(100);
        QCOMPARE(text(c->plan().toConf()), before);

        // Choose the second pad.
        question = start();
        QVERIFY(question);
        question->setTextValue("aux");
        question->accept();
        QVERIFY(waitFor([&] { return c->plan().elements.size() == 2 && !c->busy(); }, 8000));
        QCOMPARE(c->plan().links[0].sinkPad, std::string("aux"));
        QCOMPARE(c->view(text(c->plan().elements[1].name)).sinks[1].state, PadView::Linked);
        QCOMPARE(c->view(text(c->plan().elements[1].name)).sinks[0].state, PadView::Empty);
        auto* caps = shown<CapsDialog>(&w);
        QVERIFY(caps);
        press(caps, "cancelButton");
    }

    void pickerOffersExistingElementsAndRequestPads() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        QString mix = addAfter(*c, src, "src", "compositor", "sink_%u");
        QString second = addSource(*c, "videotestsrc");
        w.scene()->rebuild();
        w.view()->fitAll();
        QTest::mouseClick(w.view()->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint(w.view(), w.scene()->pad(second, "src", true)->center()));
        auto* picker = shown<PickerDialog>(&w);
        QVERIFY(picker);
        auto* list = picker->findChild<QListWidget*>("pickerList");
        QCOMPARE(list->item(0)->text(), QString("Var olan elementler"));
        QCOMPARE(list->item(1)->text(), mix + " · sink_%u");
        QCOMPARE(list->item(1)->data(HintRole).toString(), QString("Request a pad"));
        QCOMPARE(list->currentItem(), list->item(1));
        press(picker, "saveButton");  // the first row is selected: "Request a pad"
        // The new Request pad is made, an update runs, and the CapsDialog offers the pad settings.
        auto* caps = shown<CapsDialog>(&w);
        QVERIFY(caps);
        QVERIFY(caps->findChild<QWidget*>("padSettings")->isVisible());
        QCOMPARE(c->plan().links.size(), size_t(2));
        QCOMPARE(c->plan().links[1].sinkPad, std::string("sink_%u"));
        caps->findChild<QWidget*>("padEditor_xpos")->findChild<QSpinBox*>()->setValue(320);
        press(caps, "saveButton");
        QVERIFY(waitFor([&] { return !c->busy() && c->plan().links[1].sinkPadProps.count("xpos") == 1; }, 8000));
        QCOMPARE(c->plan().links[1].sinkPadProps.at("xpos"), std::string("320"));
        QCOMPARE(c->view(mix).sinks.size(), 2);
    }

    void linkMenuHasTheFourActions() {
        MainWindow w(c.get());
        w.show();
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);
        w.flows()->showLinkMenu(src, "src", QPoint(200, 200));
        auto* menu = visibleChild<QMenu>(&w);
        QVERIFY(menu);
        QCOMPARE(menu->objectName(), QString("linkMenu"));
        QStringList texts;
        for (QAction* a : menu->actions()) texts << a->text();
        QCOMPARE(texts, QStringList({"Caps düzenle", "Element değiştir", "Bağlantıyı kaldır", "Queue ekle"}));
        menu->close();

        w.flows()->showElementMenu(src, QPoint(200, 200));
        menu = visibleChild<QMenu>(&w);
        QVERIFY(menu);
        texts.clear();
        for (QAction* a : menu->actions()) texts << a->text();
        QCOMPARE(texts, QStringList({"Property düzenle", "Sil"}));  // a source can be deleted
        menu->close();
        w.flows()->showElementMenu(convert, QPoint(200, 200));
        menu = visibleChild<QMenu>(&w);
        texts.clear();
        for (QAction* a : menu->actions()) texts << a->text();
        QCOMPARE(texts, QStringList({"Property düzenle"}));  // no Sil on a plain element, no Queue kaldır
        menu->close();
    }

    void queueInsertAndRemoveThroughTheMenus() {
        MainWindow w(c.get());
        w.show();
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);
        w.flows()->showLinkMenu(src, "src", QPoint(200, 200));
        choose(&w, "actionInsertQueue");
        QVERIFY(waitFor([&] { return c->plan().elements.size() == 4 && !c->busy(); }, 8000));
        QString queue = text(c->plan().elements.back().name);
        QCOMPARE(text(c->plan().find(queue.toStdString())->factory), QString("queue"));

        w.flows()->showElementMenu(queue, QPoint(200, 200));
        QVERIFY(visibleChild<QMenu>(&w)->findChild<QAction*>("actionRemoveQueue"));
        choose(&w, "actionRemoveQueue");
        QVERIFY(waitFor([&] { return c->plan().elements.size() == 3 && !c->busy(); }, 8000));
    }

    void cutAsksFirstAndCancelChangesNothing() {
        MainWindow w(c.get());
        w.show();
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);
        QString before = text(c->plan().toConf());

        w.flows()->showLinkMenu(src, "src", QPoint(200, 200));
        choose(&w, "actionCut");
        auto* confirm = shown<ConfirmDialog>(&w);
        QVERIFY(confirm);
        auto* list = confirm->findChild<QListWidget*>("removalList");
        QCOMPARE(list->count(), 2);
        QVERIFY(list->item(0)->text().startsWith(convert));
        QVERIFY(list->item(0)->text().contains("videoconvert"));
        QVERIFY(list->item(1)->text().startsWith(sink));
        press(confirm, "cancelButton");
        QTest::qWait(50);
        QCOMPARE(text(c->plan().toConf()), before);

        w.flows()->showLinkMenu(src, "src", QPoint(200, 200));
        choose(&w, "actionCut");
        confirm = shown<ConfirmDialog>(&w);
        press(confirm, "saveButton");
        QVERIFY(waitFor([&] { return c->plan().elements.size() == 1 && !c->busy(); }, 8000));
    }

    void replaceAsksFirstAndKeepsTheLinkPad() {
        MainWindow w(c.get());
        w.show();
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);

        w.flows()->showLinkMenu(src, "src", QPoint(200, 200));
        choose(&w, "actionReplace");
        auto* picker = shown<PickerDialog>(&w);
        QVERIFY(picker);
        QVERIFY(picker->windowTitle().contains("değiştir"));
        auto* list = picker->findChild<QListWidget*>("pickerList");
        QCOMPARE(list->item(0)->text(), QString("Yeni element"));  // replacing never offers existing elements
        QTest::keyClicks(picker->findChild<QLineEdit*>("searchBox"), "queue");
        press(picker, "saveButton");
        auto* props = shown<PropertyDialog>(&w);
        QVERIFY(props);
        press(props, "saveButton");
        auto* confirm = shown<ConfirmDialog>(&w);  // after Save, before anything changes
        QVERIFY(confirm);
        QCOMPARE(c->plan().elements.size(), size_t(3));
        QCOMPARE(confirm->findChild<QListWidget*>("removalList")->count(), 2);
        press(confirm, "cancelButton");
        QTest::qWait(50);
        QCOMPARE(c->plan().elements.size(), size_t(3));
        QVERIFY(c->plan().find(convert.toStdString()));

        // Again, and this time confirmed: the old elements go, the new one sits behind the same pad.
        w.flows()->replaceBehind(src, "src");
        picker = shown<PickerDialog>(&w);
        QTest::keyClicks(picker->findChild<QLineEdit*>("searchBox"), "queue");
        press(picker, "saveButton");
        press(shown<PropertyDialog>(&w), "saveButton");
        press(shown<ConfirmDialog>(&w), "saveButton");
        QVERIFY(waitFor([&] { return c->plan().elements.size() == 2 && !c->busy(); }, 8000));
        QVERIFY(!c->plan().find(convert.toStdString()));
        QCOMPARE(c->plan().links[0].srcElement, src.toStdString());
        QCOMPARE(text(c->plan().elements[1].factory), QString("queue"));
        QVERIFY(shown<CapsDialog>(&w));  // the update succeeded, so the link's caps are offered
    }

    void deleteSourceAsksFirst() {
        MainWindow w(c.get());
        w.show();
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);
        w.flows()->showElementMenu(src, QPoint(200, 200));
        choose(&w, "actionDelete");
        auto* confirm = shown<ConfirmDialog>(&w);
        QVERIFY(confirm);
        QCOMPARE(confirm->findChild<QListWidget*>("removalList")->count(), 3);  // source + everything behind it
        press(confirm, "saveButton");
        QVERIFY(waitFor([&] { return c->plan().empty(); }, 8000));
        QVERIFY(!c->canSave());
    }

    void editPropertiesByDoubleClickAndExpose() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        w.scene()->rebuild();
        w.view()->fitAll();
        QPointF body = w.scene()->element(src)->pos() + QPointF(120, 12);
        doubleClick(w.view()->viewport(), viewPoint(w.view(), body));
        auto* dialog = shown<PropertyDialog>(&w);
        QVERIFY(dialog);
        QVERIFY(dialog->windowTitle().contains(src));
        in<QComboBox>(*dialog, "editor_pattern")->setCurrentIndex(1);
        dialog->findChild<QCheckBox*>("expose_num-buffers")->click();
        press(dialog, "saveButton");
        QVERIFY(waitFor([&] { return !c->busy() && c->plan().exposed.size() == 1; }, 8000));
        QCOMPARE(c->plan().find(src.toStdString())->props.at("pattern"), std::string("snow"));
        QCOMPARE(c->plan().exposed[0].property, std::string("num-buffers"));
        QCOMPARE(c->plan().exposed[0].name, std::string("num-buffers"));
    }

    void editCapsFromTheMenu() {
        MainWindow w(c.get());
        w.show();
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);
        w.flows()->showLinkMenu(src, "src", QPoint(200, 200));
        choose(&w, "actionEditCaps");
        auto* caps = shown<CapsDialog>(&w);
        QVERIFY(caps);
        caps->findChild<QComboBox*>("mediaCombo")->setCurrentIndex(1);
        auto* width = caps->findChild<QWidget*>("field_width");
        width->findChild<QCheckBox*>()->click();
        width->findChild<QSpinBox*>()->setValue(1280);
        press(caps, "saveButton");
        QVERIFY(waitFor([&] { return !c->busy() && !c->plan().links[0].caps.empty(); }, 8000));
        QCOMPARE(c->plan().links[0].caps, std::string("video/x-raw, width=(int)1280"));
        QVERIFY(c->result().isComplete());
        // Open it again: the dialog starts from the choice.
        w.flows()->editCaps(src, "src");
        caps = shown<CapsDialog>(&w);
        QVERIFY(caps);
        QCOMPARE(caps->caps(), QString("video/x-raw, width=(int)1280"));
        press(caps, "cancelButton");
    }

    void everythingIsRefusedWhileBusy() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        need(*c, "fakesink");
        QVERIFY(c->addAfter(src, "src", "fakesink", "sink", {}));
        QVERIFY(c->updating());
        w.flows()->fromFreePad(src, "src");
        w.flows()->showLinkMenu(src, "src", QPoint(10, 10));
        w.flows()->showElementMenu(src, QPoint(10, 10));
        w.flows()->deleteSource(src);
        w.flows()->editProperties(src);
        w.flows()->chooseSource();
        w.flows()->addSource("audiotestsrc");
        QTest::qWait(50);
        QVERIFY(!visibleChild<QMenu>(&w));
        QVERIFY(!visibleChild<QDialog>(&w));
        QVERIFY(settle(*c));
        QCOMPARE(c->plan().elements.size(), size_t(2));
    }

    void runFromTheToolbarLogsTheHelper() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        addAfter(*c, src, "src", "fakesink");
        auto* run = w.findChild<QAction*>("actionRun");
        QVERIFY(run->isEnabled());
        QCOMPARE(run->text(), QString("Çalıştır"));
        QSignalSpy ended(c.get(), &Controller::runFinished);
        run->trigger();
        QCOMPARE(run->text(), QString("Durdur"));
        QVERIFY(waitFor([&] { return ended.count() == 1; }));
        QCOMPARE(run->text(), QString("Çalıştır"));
        QVERIFY(w.logPanel()->toPlainText().contains("Çalıştırma bitti: eos"));
    }

    void openFileFitsAndShowsThePlan() {
        QString src, convert, sink;
        QTemporaryDir dir;
        QString path = dir.filePath("keep.conf");
        {
            src = addSource(*c, "videotestsrc");
            convert = addAfter(*c, src, "src", "videoconvert");
            sink = addAfter(*c, convert, "src", "fakesink");
            QString error;
            QVERIFY(c->save(path, &error));
        }
        auto other = makeController();
        MainWindow w(other.get());
        w.show();
        w.openFile(path);
        QVERIFY(waitFor([&] { return other->result().valid() && !other->busy() && w.scene()->element(sink) != nullptr; }, 8000));
        QCOMPARE(w.scene()->links().size(), 2);
        QCOMPARE(w.windowTitle(), QString("GstPipeStudio – keep.conf"));
        QVERIFY(w.findChild<QAction*>("actionRun")->isEnabled());
        QTest::qWait(100);
        // fitAll ran: the whole pipeline is inside the viewport
        QRectF items = w.scene()->itemsBoundingRect();
        QVERIFY(w.view()->viewport()->rect().contains(w.view()->mapFromScene(items).boundingRect().adjusted(5, 5, -5, -5)));
    }

private:
    // The scene rebuilds when the event loop turns; tests that look at it right away rebuild by hand.
    void scene_rebuild(MainWindow& w) {
        w.scene()->rebuild();
        w.view()->fitAll();
    }
};

QTEST_MAIN(WidgetsTest)
#include "widgets_test.moc"
