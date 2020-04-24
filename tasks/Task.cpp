/* Generated from orogen/lib/orogen/templates/tasks/Task.cpp */

#include "Task.hpp"

#include <base-logging/Logging.hpp>

#include <seasocks/PrintfLogger.h>
#include <seasocks/WebSocket.h>
#include <seasocks/Server.h>

#include <string.h>
#include <iostream>

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
        msg["result"] = false;
        if (connection == socket) {
            ++task->received;
            msg["result"] = task->handleIncomingWebsocketMessage(data);
            // Increment errors count if the result is false, do nothing otherwise
            task->errors += msg["result"].asBool() ? 0 : 1;
        }
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
        return jdata[mapFieldName(mapping.type)][mapping.index].asDouble();
    }
};

bool Task::handleIncomingWebsocketMessage(char const* data) {
    std::string errs;
    if (!decoder->parseJSONMessage(data, errs)) {
        LOG_ERROR_S << "Failed parsing the message, got error: " << errs << std::endl;
        return false;
    }

    auto is_valid = is_controlling ? decoder->validateControlMessage() :
                                     decoder->validateAskControlMessage();
    if (!is_valid) {
        return false;
    }

    auto is_filled = updateRawCommand();
    if (is_filled and is_controlling) {
        _raw_command.write(raw_cmd_obj);
    }

    is_controlling = is_controlling ? true : is_filled;
    return is_filled;
}

// Fill the Raw Command with the JSON data at decoder.
bool Task::updateRawCommand(){
    try{
        for (uint i = 0; i < axis->size(); ++i){
            raw_cmd_obj.axisValue.at(i) = decoder->getValue(axis->at(i));
        }
        for (uint i = 0; i < button->size(); ++i){
            raw_cmd_obj.buttonValue.at(i) = decoder->getValue(button->at(i))
                                                              > button->at(i).threshold;
        }

    // A failure here means that the client sent a bad message or the mapping isn't good.
    // Just inform the client: return false.
    } catch (const std::exception &e){
        LOG_ERROR_S << "Invalid message, got error: " << e.what() << std::endl;
        return false;
    }
    return true;
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
