#include "appservices.h"

AppServices::AppServices(DocumentTiming timing, QObject* parent)
    : AppServices(timing, std::make_unique<CMakePluginBuilder>(CMakePluginBuilder::Options{
          qEnvironmentVariable("GPS_ELEMENT_SDK_DIR"), GPS_ELEMENT_SDK_BUILD_DIR}), parent) {}

AppServices::AppServices(DocumentTiming timing, std::unique_ptr<IPluginBuilder> builder, QObject* parent)
    : QObject(parent), timing_(timing), pluginBuilder_(std::move(builder)), plugins_(catalog_, *pluginBuilder_) {}
