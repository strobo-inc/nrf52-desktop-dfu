#pragma once

#include <string>

bool validate_mac_address(const std::string& address);
bool is_mac_addr_match(const std::string& device_addr, const std::string& input_addr);
