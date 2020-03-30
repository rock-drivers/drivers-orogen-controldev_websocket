/* Generated from orogen/lib/orogen/templates/tasks/Task.cpp */

#include "Task.hpp"

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
    void onConnect(WebSocket *socket) override{
        if (connection != nullptr){
            connection->close();
        }
        connection = socket;
    }
    void onData(WebSocket *socket, const char *data) override{
        Json::Value msg;
        if (connection == socket){
            msg["result"] = Json::Value(task->updateRawCommand(data));
            if (msg["result"].asBool() && task->controlling){
                task->raw_cmd_obj.time = base::Time::now();
                task->_raw_command.write(task->raw_cmd_obj);
            }
        } else {
            msg["result"] = false;
        }
        Json::FastWriter fast;
        connection->send(fast.write(msg));
    }
    void onDisconnect(WebSocket *socket) override{
        if (connection == socket){
            connection = nullptr;
        }
    }
};

bool Task::updateRawCommand(const char *data){
    Json::Value jdata;
    Json::CharReaderBuilder rbuilder;
    std::unique_ptr<Json::CharReader> const reader(rbuilder.newCharReader());
    std::string errs;

    if (!reader->parse(data, data+std::strlen(data), &jdata, &errs)){
        std::cout << errs << std::endl;
        return false;
    }
    if (!(jdata.isMember("status") && jdata.isMember("axes") && jdata.isMember("buttons"))) {
        return false;
    }
    try{
        controlling = jdata["status"].asBool();

        raw_cmd_obj.axisValue[0] = jdata["axes"][0].asDouble();
        raw_cmd_obj.axisValue[1] = jdata["axes"][1].asDouble();
        raw_cmd_obj.axisValue[2] = jdata["buttons"][6].asDouble() * 2 - 1;
        raw_cmd_obj.axisValue[3] = jdata["axes"][2].asDouble();
        raw_cmd_obj.axisValue[4] = jdata["axes"][3].asDouble();
        raw_cmd_obj.axisValue[5] = jdata["buttons"][7].asDouble() * 2 - 1;
        raw_cmd_obj.axisValue[6] = jdata["buttons"][15].asDouble() - jdata["buttons"][14].asDouble();
        raw_cmd_obj.axisValue[7] = jdata["buttons"][13].asDouble() - jdata["buttons"][12].asDouble();

        raw_cmd_obj.buttonValue[0] = jdata["buttons"][0].asDouble() > _button_threshold.get();
        raw_cmd_obj.buttonValue[1] = jdata["buttons"][1].asDouble() > _button_threshold.get();
        raw_cmd_obj.buttonValue[2] = jdata["buttons"][2].asDouble() > _button_threshold.get();
        raw_cmd_obj.buttonValue[3] = jdata["buttons"][3].asDouble() > _button_threshold.get();
        raw_cmd_obj.buttonValue[4] = jdata["buttons"][4].asDouble() > _button_threshold.get();
        raw_cmd_obj.buttonValue[5] = jdata["buttons"][5].asDouble() > _button_threshold.get();
        raw_cmd_obj.buttonValue[6] = jdata["buttons"][8].asDouble() > _button_threshold.get();
        raw_cmd_obj.buttonValue[7] = jdata["buttons"][9].asDouble() > _button_threshold.get();

    } catch (std::exception e){
        std::cout << e.what() << std::endl;
        return false;
    }
    return true;
}

Task::Task(std::string const& name, TaskCore::TaskState initial_state)
    : TaskBase(name, initial_state)
{
    for (int i=1; i<=8; ++i){
        raw_cmd_obj.buttonValue.push_back(0);
        raw_cmd_obj.axisValue.push_back(0.0);
    }
}

Task::~Task()
{
}

bool Task::configureHook()
{
    if (! TaskBase::configureHook()){
        return false;
    }

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
    return true;
}
void Task::updateHook()
{
    TaskBase::updateHook();
}
void Task::errorHook()
{
    TaskBase::errorHook();
    server->terminate();
    thread->join();
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
}
