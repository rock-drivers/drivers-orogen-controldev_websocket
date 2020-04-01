/* Generated from orogen/lib/orogen/templates/tasks/Task.cpp */

#include "Task.hpp"
#include "../Mapping.hpp"

#include <base-logging/Logging.hpp>

#include <seasocks/PrintfLogger.h>
#include <seasocks/WebSocket.h>
#include <seasocks/Server.h>

#include <json/json.h>

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
    void onConnect(WebSocket *socket) override{
        if (connection != nullptr){
            connection->close();
        }
        connection = socket;
    }
    void onData(WebSocket *socket, const char *data) override{
        msg["result"] = false;
        if (connection == socket){
            ++task->received;
            msg["result"] = Json::Value(task->updateRawCommand(data));
            if (msg["result"].asBool() && task->controlling){
                task->raw_cmd_obj.time = base::Time::now();
                task->_raw_command.write(task->raw_cmd_obj);
            } else {
                ++task->errors;
            }
        }
        connection->send(fast.write(msg));
    }
    void onDisconnect(WebSocket *socket) override{
        if (connection == socket){
            connection = nullptr;
        }
    }
};

struct controldev_websocket::WrapperJSON{
    Json::Value jdata;
    Json::CharReaderBuilder rbuilder;
    std::unique_ptr<Json::CharReader> const reader;

    WrapperJSON() : reader(rbuilder.newCharReader()) {

    }

    double getValue(const std::string &field, int index){
        return jdata[field][index].asDouble();
    }
};

bool Task::updateRawCommand(const char *data){
    std::string errs;
    // If an error occurs trying to get the parameters should not ignore it.
    auto axis = _axis_map.get();
    auto button = _button_map.get();

    if (!wrapper->reader->parse(data, data+std::strlen(data), &wrapper->jdata, &errs)){
        LOG_ERROR_S << "Failed parsing the message, got error: " << errs << std::endl;
        return false;
    }
    if (!wrapper->jdata.isMember("status") ||
        !wrapper->jdata.isMember(Mapping::mapFieldName(Mapping::Type::Axis)) ||
        !wrapper->jdata.isMember(Mapping::mapFieldName(Mapping::Type::Button))) {
        LOG_ERROR_S << "Invalid message, it doesn't contain the required fields.";
        LOG_ERROR_S << std::endl;
        return false;
    }
    try{
        controlling = wrapper->jdata["status"].asBool();

        for (uint i = 0; i < axis.size(); ++i){
            raw_cmd_obj.axisValue.at(i) = wrapper->getValue(axis.at(i).getFieldName(),
                                                           axis.at(i).index);
        }
        for (uint i = 0; i < button.size(); ++i){
            raw_cmd_obj.buttonValue.at(i) = wrapper->getValue(button.at(i).getFieldName(),
                                                             button.at(i).index)
                                                             > button.at(i).threshold;
        }

    // An exception here means a failure while converting the Gamepad. Just return false.
    } catch (std::exception e){
        LOG_ERROR_S << "Invalid message, got error: " << e.what() << std::endl;
        return false;
    }
    return true;
}

Task::Task(std::string const& name, TaskCore::TaskState initial_state)
    : TaskBase(name, initial_state)
{
    raw_cmd_obj.buttonValue.resize(_button_map.get().size(), 0);
    raw_cmd_obj.axisValue.resize(_axis_map.get().size(), 0.0);
}

Task::~Task()
{
}

bool Task::configureHook()
{
    if (! TaskBase::configureHook()){
        return false;
    }
    wrapper = new WrapperJSON();
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
    _received.write(received);
    _errors.write(errors);
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
    delete wrapper;
}
