#pragma once

#include <condition_variable>
#include <mutex>
#include <string>
#include "NrfDfuServerTypes.h"

namespace NativeDFU {
class NrfDfuTrigger {
  public:
    /*
     * @param write_command_p: callback to be called for writing a ble command
     * @param write_request_p: callback to be called for writing a ble request
     * */
    NrfDfuTrigger(ble_write_t write_command_p, ble_write_t write_request_p, const std::string& dfu_target_adv_name);
    ~NrfDfuTrigger();

    /**
     * NrfDfuTrigger::indicate
     *
     * This function receive indication of Buttonless Secure DFU Service characteristics
     *
     * @param service: BLE service & characteristic which sent data
     * @param characteristic: BLE service & characteristic which sent data
     * @param data: Raw data received via BLE
     */
    void indicate(std::string service, std::string characteristic, std::string data);
    /*
    NrfDfuTrigger::run
    run trigger process
    return true if success
    */
    bool run(void);

  private:
    typedef enum { INIT, ADVNAME_SENT, DFU_TRIGGERED, COMPLETED, ERROR } dfu_trig_state_t;
    dfu_trig_state_t state;
    dfu_trig_state_t update(dfu_trig_state_t s, const std::string& resp);
    dfu_trig_state_t op_init();
    dfu_trig_state_t op_advname_sent(const std::string& resp);
    dfu_trig_state_t op_dfu_triggered(const std::string& resp);
    bool is_reply_pkt_ok(const std::string& resp, uint8_t expected_opcode);
    // * Callbacks to write commands & request: This allows the DFU Server to be agnostic from the BLE implementation
    ble_write_t write_command;
    ble_write_t write_request;
    std::string target_adv_name;
    std::mutex mtx;
    std::condition_variable cv;
};

}  // namespace NativeDFU