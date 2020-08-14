/* Generated from orogen/lib/orogen/templates/tasks/Task.cpp */

#include "Task.hpp"

#include <base-logging/Logging.hpp>

#include <seasocks/PrintfLogger.h>
#include <seasocks/Server.h>
#include <seasocks/WebSocket.h>

#include <string.h>
#include <iostream>
#include <json/json.h>

using namespace controldev_websocket;
using namespace controldev;
using namespace seasocks;

struct controldev_websocket::JoystickHandler : WebSocket::Handler {
    WebSocket *connection = nullptr;
    Task *task = nullptr;
    Json::Value msg;
    Json::FastWriter fast;
    Statistics statistic;

    void onConnect(WebSocket *socket) override{
        if (connection != nullptr){
            connection->close();
        }
        connection = socket;
        task->is_controlling = false;
    }
    void onData(WebSocket *socket, const char *data) override{
        if (connection != socket) {
            LOG_WARN_S << "Received message from inactive connection" << std::endl;
            return;
        }

        ++task->received;
        bool result = task->handleIncomingWebsocketMessage(data, socket);
        msg["result"] = result;
        // Increment errors count if the result is false, do nothing otherwise
        task->errors += result ? 0 : 1;
        statistic.received = task->received;
        statistic.errors = task->errors;
        statistic.time = base::Time::now();
        task->_statistics.write(statistic);
        connection->send(fast.write(msg));
    }
    void onDisconnect(WebSocket *socket) override{
        if (connection == socket){
            connection = nullptr;
        }
    }
};

struct controldev_websocket::MessageDecoder{
    Json::Value jdata;
    Json::CharReaderBuilder rbuilder;
    std::unique_ptr<Json::CharReader> const reader;

    MessageDecoder() : reader(rbuilder.newCharReader()) {

    }

    static std::string mapFieldName(Mapping::Type type){
        if (type == Mapping::Type::Button){
            return "buttons";
        }
        if (type == Mapping::Type::Axis){
            return "axes";
        }
        throw "Failed to get Field Name. The type set is invalid";
    }

    bool parseJSONMessage(char const* data, std::string &errors){
        return reader->parse(data, data+std::strlen(data), &jdata, &errors);
    }

    bool validateControlMessage(){
        if (!jdata.isMember(mapFieldName(Mapping::Type::Axis)) ||
            !jdata.isMember(mapFieldName(Mapping::Type::Button))) {
            LOG_ERROR_S << "Invalid message, it doesn't contain the required fields.";
            LOG_ERROR_S << std::endl;
            return false;
        }
        return true;
    }

    bool validateAskControlMessage(){
        if (!jdata.isMember("test_message")) {
            LOG_ERROR_S << "Invalid test msg, it doesn't contain the test_message field.";
            LOG_ERROR_S << std::endl;
            return false;
        }
        jdata = jdata["test_message"];
        return validateControlMessage();
    }

    double getValue(const Mapping &mapping){
        auto const& field = jdata[mapFieldName(mapping.type)];
        if (!field.isValidIndex(mapping.index)) {
            throw std::out_of_range(
                "incoming raw command does not have a field " +
                std::to_string(mapping.index) + "in " + mapFieldName(mapping.type)
            );
        }
        return field[mapping.index].asDouble();
    }

    base::Time getTime(){
        return base::Time::fromMilliseconds(
            jdata["time"].asUInt64()
        );
    }
};

bool Task::handleIncomingWebsocketMessage(char const* data, WebSocket *connection) {
    std::string errs;
    if (!decoder->parseJSONMessage(data, errs)) {
        LOG_ERROR_S << "Failed parsing the message, got error: " << errs << std::endl;
        return false;
    }

    if (is_controlling) {
        return handleControlMessage();
    }
    else {
        return handleAskControlMessage();
    }
}

bool Task::handleControlMessage() {
    if (!decoder->validateControlMessage()){
        return false;
    }

    if (!updateRawCommand()){
        return false;
    }
    _raw_command.write(raw_cmd_obj);
    return true;
}

bool Task::handleAskControlMessage() {
    if (!decoder->validateAskControlMessage()){
        return false;
    }

    if (!updateRawCommand()){
        return false;
    }

    is_controlling = true;
    return true;
}

// Fill the Raw Command with the JSON data at decoder.
bool Task::updateRawCommand(){
    try {
        for (uint i = 0; i < axis->size(); ++i) {
            raw_cmd_obj.axisValue.at(i) = decoder->getValue(axis->at(i));
        }
        for (uint i = 0; i < button->size(); ++i){
            raw_cmd_obj.buttonValue.at(i) =
                decoder->getValue(button->at(i)) > button->at(i).threshold;
        }
        raw_cmd_obj.time = decoder->getTime();
        return true;
    }
    // A failure here means that the client sent a bad message or the
    // mapping isn't good. Just inform the client: return false.
    catch (const std::exception &e) {
        LOG_ERROR_S << "Invalid message, got error: " << e.what() << std::endl;
        return false;
    }
}

Task::Task(std::string const& name, TaskCore::TaskState initial_state)
    : TaskBase(name, initial_state)
{
}

Task::~Task()
{
}

bool Task::configureHook()
{
    if (! TaskBase::configureHook()){
        return false;
    }

    received = 0;
    errors = 0;

    button = new std::vector<ButtonMapping>(_button_map.get());
    axis = new auto(_axis_map.get());

    raw_cmd_obj.buttonValue.resize(button->size(), 0);
    raw_cmd_obj.axisValue.resize(axis->size(), 0.0);

    decoder = new MessageDecoder();
    auto logger = std::make_shared<PrintfLogger>(Logger::Level::Debug);
    server = new Server (logger);
    handler = std::make_shared<JoystickHandler>();
    handler->task = this;

    return true;
}
bool Task::startHook()
{
    if (! TaskBase::startHook()){
        return false;
    }

    server->addWebSocketHandler("/ws", handler, true);
    if (!server->startListening(_port.get())) {
        return false;
    }

    thread = new std::thread([this]{ this->server->loop(); });

    return true;
}
void Task::updateHook()
{
    TaskBase::updateHook();
}
void Task::errorHook()
{
    TaskBase::errorHook();
}
void Task::stopHook()
{
    TaskBase::stopHook();
    server->terminate();
    thread->join();
}
void Task::cleanupHook()
{
    TaskBase::cleanupHook();
    delete server;
    delete thread;
    delete decoder;
}
