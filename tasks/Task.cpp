/* Generated from orogen/lib/orogen/templates/tasks/Task.cpp */

#include "Task.hpp"

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
    Statistics statistic;

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

struct controldev_websocket::WrapperJSON{
    Json::Value jdata;
    Json::CharReaderBuilder rbuilder;
    std::unique_ptr<Json::CharReader> const reader;

    WrapperJSON() : reader(rbuilder.newCharReader()) {

    }

    double getValue(Mapping::Type type, int index){
        return jdata[mapFieldName(type)][index].asDouble();
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
};

bool Task::updateRawCommand(const char *data){
    std::string errs;

    if (!wrapper->reader->parse(data, data+std::strlen(data), &wrapper->jdata, &errs)){
        LOG_ERROR_S << "Failed parsing the message, got error: " << errs << std::endl;
        return false;
    }
    if (!wrapper->jdata.isMember("status") ||
        !wrapper->jdata.isMember(WrapperJSON::mapFieldName(Mapping::Type::Axis)) ||
        !wrapper->jdata.isMember(WrapperJSON::mapFieldName(Mapping::Type::Button))) {
        LOG_ERROR_S << "Invalid message, it doesn't contain the required fields.";
        LOG_ERROR_S << std::endl;
        return false;
    }
    try{
        controlling = wrapper->jdata["status"].asBool();

        for (uint i = 0; i < axis->size(); ++i){
            raw_cmd_obj.axisValue.at(i) = wrapper->getValue(axis->at(i).type,
                                                            axis->at(i).index);
        }
        for (uint i = 0; i < button->size(); ++i){
            raw_cmd_obj.buttonValue.at(i) = wrapper->getValue(button->at(i).type,
                                                              button->at(i).index)
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

    button = new std::vector<ButtonMapping>(_button_map.get());
    axis = new auto(_axis_map.get());

    raw_cmd_obj.buttonValue.resize(button->size(), 0);
    raw_cmd_obj.axisValue.resize(axis->size(), 0.0);

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
