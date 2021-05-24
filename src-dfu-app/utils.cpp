#include "utils.h"

bool validate_mac_address(const std::string& address) { return address.length() >= 4; }

// Assuming the input mac address is at least 4 characters
bool is_mac_addr_match(const std::string& device_addr, const std::string& input_addr) {
    for (int i = 0; i < input_addr.length(); i++) {
        if (device_addr[i] != input_addr[i]) return false;
    }
    return true;
}