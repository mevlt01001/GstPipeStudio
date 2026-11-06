#include "gst_engine.h"
#include "check.h"
using namespace gps;

int main(int argc,char** argv) {
    gst_init(&argc,&argv);
    Pipeline document;
    auto& graph=document.editor().begin();
    auto source=graph.addElement("videotestsrc",{{"num-buffers","5"}});
    graph.addElement("videoconvert"); // disconnected work stays in the document
    CHECK(document.editor().commit());
    GstEngine engine({3000,{}},{});
    auto plan=document.executionPlan(&GstElementCreator::queryFactoryTraits,1);
    CHECK_EQ(plan.graph().elements.size(),size_t(1));
    std::string original=document.graph().toConf();
    bool completed=false;
    engine.execute(plan,[&](ExecutionResult result){
        completed=true;
        CHECK(result.ticket==plan.ticket());
        CHECK_EQ(result.report.status,std::string("ok"));
        CHECK(!result.report.complete());
        CHECK(document.apply(result,plan.ticket()));
    });
    CHECK(completed);CHECK_EQ(document.graph().toConf(),original);
    const auto* element=document.graph().find(source);
    CHECK(element);CHECK(!element->pads.empty());
    CHECK(element->pads[0].negotiatedCaps.find("video/x-raw")!=std::string::npos);
    auto& draft=document.editor().begin();
    auto sink=draft.addElement("fakesink");
    CHECK(draft.linkExisting(source,"src",sink,"sink","video/x-raw, width=(int)64"));
    CHECK(document.editor().commit());
    auto next=document.executionPlan(&GstElementCreator::queryFactoryTraits,2);
    engine.execute(next,[&](ExecutionResult result){
        CHECK_EQ(result.report.status,std::string("ok"));CHECK(result.report.complete());
        CHECK(document.apply(result,next.ticket()));
    });
    CHECK_EQ(document.graph().elements.size(),size_t(3)); // synthetic capsfilter was not authored
    auto peer=document.peer({source,"src",Pad::Direction::Source});
    CHECK(peer.has_value());CHECK_EQ(peer->element,sink);
    CHECK_EQ(document.graph().links[0].caps,std::string("video/x-raw, width=(int)64"));
    return finish("backend_test");
}
