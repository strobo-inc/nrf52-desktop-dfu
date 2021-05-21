#pragma once
#include "NrfDfuTrigger.h"
#include <vector>
#include <iostream>

using namespace NativeDFU;

NrfDfuTrigger::NrfDfuTrigger(ble_write_t write_command_p, ble_write_t write_request_p,
                             const std::string& dfu_target_adv_name)
    : write_command(write_command_p),
      write_request(write_request_p),
      target_adv_name(dfu_target_adv_name),
      state(INIT) {}
NrfDfuTrigger::~NrfDfuTrigger() {}

/**
 * NrfDfuTrigger::indicate
 *
 * This function receive indication of Buttonless Secure DFU Service characteristics
 *
 * @param service: BLE service & characteristic which sent data
 * @param characteristic: BLE service & characteristic which sent data
 * @param data: Raw data received via BLE
 */
void NrfDfuTrigger::indicate(std::string service, std::string characteristic, std::string data) {
    if (service != NORDIC_SECURE_DFU_SERVICE) {
        return;
    }
    if (characteristic != NORDIC_DFU_BUTTONLESS_CHAR) {
        return;
    }
    std::unique_lock<std::mutex> lk(mtx);
    state = update(state, data);
    lk.unlock();
    cv.notify_all();
}
NrfDfuTrigger::dfu_trig_state_t NrfDfuTrigger::update(NrfDfuTrigger::dfu_trig_state_t s, const std::string& resp) {
    switch (s) {
        case INIT:
            /* code */
            return op_init();
            break;
        case ADVNAME_SENT:
            return op_advname_sent(resp);
            break;
        case DFU_TRIGGERED:
            return op_dfu_triggered(resp);
            break;
        default:
            break;
    }
    return ERROR;
}

bool NrfDfuTrigger::is_reply_pkt_ok(const std::string& resp, uint8_t expected_opcode){
    if (resp.length() != 3) {
        return ERROR;
    }
    uint8_t resp_opcode = resp[1];
    uint8_t resp_status = resp[2];
    if ((resp_opcode != expected_opcode) || (resp_status != DFU_RSP_SUCCESS)) {
        std::cout<<"dfu trigger error opcode="<<(int)expected_opcode<<", returned opcode="<<(int)resp_opcode<<", errcode="<<(int)resp_status<<std::endl;
        return false;
    }
    return true;
}
NrfDfuTrigger::dfu_trig_state_t NrfDfuTrigger::op_init() {
    std::vector<uint8_t> setadvname_pkt(1 + 1 + target_adv_name.length());
    setadvname_pkt[0] = DFU_OP_SET_ADV_NAME;
    setadvname_pkt[1] = target_adv_name.length();
    memcpy(&setadvname_pkt[2], target_adv_name.data(), target_adv_name.length());
    std::string pkt((char*)setadvname_pkt.data());
    write_request(NORDIC_SECURE_DFU_SERVICE, NORDIC_DFU_BUTTONLESS_CHAR, pkt);
    return ADVNAME_SENT;
}
NrfDfuTrigger::dfu_trig_state_t NrfDfuTrigger::op_advname_sent(const std::string& resp) {
    if(!is_reply_pkt_ok(resp,DFU_OP_SET_ADV_NAME)){
        return ERROR;
    }
    std::cout<<"dfu trigger advname set ok."<<std::endl;
    uint8_t opcode = DFU_OP_ENTER_BOOTLOADER;
    std::string pkt((char*)&opcode);
    write_request(NORDIC_SECURE_DFU_SERVICE, NORDIC_DFU_BUTTONLESS_CHAR, pkt);
    return DFU_TRIGGERED;
}
NrfDfuTrigger::dfu_trig_state_t NrfDfuTrigger::op_dfu_triggered(const std::string& resp) {
    if(!is_reply_pkt_ok(resp,DFU_OP_ENTER_BOOTLOADER)){
        return ERROR;
    }
    std::cout<<"dfu trigger completed."<<std::endl;
    return COMPLETED;
}
/*
NrfDfuTrigger::run
run trigger process
*/
bool NrfDfuTrigger::run(void) {
    state = update(state,"");
    std::unique_lock<std::mutex> lk(mtx);
    cv.wait(lk,[&]{
        return (this->state==COMPLETED)||(this->ERROR);
    });
    if(state==COMPLETED){
        return true;
    }
    return false;
}