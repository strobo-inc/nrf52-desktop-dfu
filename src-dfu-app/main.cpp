#include "NativeBleController.h"
#include "NrfDfuServer.h"
#include "NrfDfuTrigger.h"
#include "json/json.hpp"
#include "miniz/miniz.h"
#include "utils.h"

#include <cerrno>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#define SCAN_DURATION_MS 10000
#define SCAN_DURATION_TRIG_MS 10000
#define DFU_TARGET_NAME "DfuTarg"


static std::string ToHex(const std::string &s, bool upper_case) {  // Used for debugging
    std::ostringstream ret;
    for (std::string::size_type i = 0; i < s.length(); ++i) {
        int z = s[i] & 0xff;
        ret << std::hex << std::setfill('0') << std::setw(2) << (upper_case ? std::uppercase : std::nouppercase) << z;
    }
    return ret.str();
}
static bool get_bin_dat_files(std::string&, std::string&, const char*);

bool do_dfu_trigger(const std::string& device_name, const std::string& device_address) {
    bool device_found = false;
    std::string device_dfu_ble_address;
    NativeBLE::NativeBleController ble;
    NativeBLE::CallbackHolder callback_holder;
    NativeDFU::NrfDfuTrigger dfu_trigger(
        [&](std::string service, std::string characteristic, std::string data) {
            std::cout << "write command:" << service << "," << characteristic << "," << ToHex(data,true) << std::endl;
            ble.write_command(service, characteristic, data);
        },
        [&](std::string service, std::string characteristic, std::string data) {
            std::cout << "write data:" << service << "," << characteristic << "," << ToHex(data,true) << std::endl;
            ble.write_request(service, characteristic, data);
        },
        device_name);

    callback_holder.callback_on_scan_found = [&](NativeBLE::DeviceDescriptor device) {
        if (is_mac_addr_match(device.address, device_address)) {
            std::cout << "  Found: " << device.name << " (" << device.address << ")" << std::endl;
            device_found = true;
            device_dfu_ble_address = device.address;
        } else {
            std::cout << "  Found: " << device.name << " (" << device.address << ") (not target)" << std::endl;
        }
    };
    std::cout << "Starting Scan for DFU target device! " << std::endl;
    ble.setup(callback_holder);
    std::this_thread::sleep_for(std::chrono::seconds(5));  // wait for bluetooth controller on.
    ble.scan_timeout(SCAN_DURATION_TRIG_MS);

    if (!device_found) {
        std::cerr << "  Device " << device_dfu_ble_address << " could not be found." << std::endl;
        ble.dispose();
        return false;
    } else {
        std::cout << "  Device " << device_dfu_ble_address << " connecting..." << std::endl;
        ble.connect(device_dfu_ble_address);
        std::cout << "  Connected to " << device_dfu_ble_address << "... initiating trigger..." << std::endl;
        ble.indicate(NORDIC_SECURE_DFU_SERVICE, NORDIC_DFU_BUTTONLESS_CHAR, [&](const uint8_t* data, uint32_t length) {
            std::ostringstream received_data;
            received_data << "Received length " << length << ": 0x";
            for (int i = 0; i < length; i++) {
                received_data << std::setfill('0') << std::setw(2) << std::hex << static_cast<unsigned int>(data[i]);
                received_data << " ";
            }
            received_data << std::endl;
            std::cout << received_data.str();
            // std::cout << "Calling Notify" << std::endl;
            dfu_trigger.indicate(NORDIC_SECURE_DFU_SERVICE, NORDIC_DFU_BUTTONLESS_CHAR,
                                 std::string(reinterpret_cast<const char*>(data), length));
        });
        std::this_thread::sleep_for(std::chrono::seconds(1));//wait for indication enable
        bool is_success = dfu_trigger.run();
        std::this_thread::sleep_for(std::chrono::seconds(1));//wait for indication confirm
        ble.disconnect();
        ble.dispose();
        if (is_success) {
            std::cout << "DFU Successfully triggered" << std::endl;
        } else {
            std::cout << "DFU trigger failed" << std::endl;
        }
        return is_success;
    }
}

bool do_dfu(const std::string& device_name, const std::string& data_file, const std::string& bin_file) {
    bool device_found = false;
    std::string device_dfu_ble_address;
    NativeBLE::NativeBleController ble;
    NativeBLE::CallbackHolder callback_holder;
    NativeDFU::NrfDfuServer dfu_server(
        [&](std::string service, std::string characteristic, std::string data) {
            std::cout << "write command:" << service << "," << characteristic << "," << data << std::endl;
            ble.write_command(service, characteristic, data);
        },
        [&](std::string service, std::string characteristic, std::string data) {
            std::cout << "write data:" << service << "," << characteristic << "," << data << std::endl;
            ble.write_request(service, characteristic, data);
        },
        data_file, bin_file);

    callback_holder.callback_on_scan_found = [&](NativeBLE::DeviceDescriptor device) {
        if (device_name == device.name) {
            std::cout << "  Found: " << device.name << " (" << device.address << ")" << std::endl;
            device_found = true;
            device_dfu_ble_address = device.address;
        } else {
            std::cout << "  Found: " << device.name << " (" << device.address << ") (not target)" << std::endl;
        }
    };

    std::cout << "Starting Scan for DFU target device! " << std::endl;
    ble.setup(callback_holder);
    std::this_thread::sleep_for(std::chrono::seconds(5));  // wait for bluetooth controller on.
    ble.scan_timeout(SCAN_DURATION_MS);

    if (!device_found) {
        std::cerr << "  Device " << device_dfu_ble_address << " could not be found." << std::endl;
        ble.dispose();
        return false;
    } else {
        ble.connect(device_dfu_ble_address);
        std::cout << "  Connected to " << device_dfu_ble_address << "... initiating streaming..." << std::endl;

        ble.notify(NORDIC_SECURE_DFU_SERVICE, NORDIC_DFU_CONTROL_POINT_CHAR, [&](const uint8_t* data, uint32_t length) {
            std::ostringstream received_data;
            received_data << "Received length " << length << ": 0x";
            for (int i = 0; i < length; i++) {
                received_data << std::setfill('0') << std::setw(2) << std::hex << static_cast<unsigned int>(data[i]);
                received_data << " ";
            }
            received_data << std::endl;
            std::cout << received_data.str();
            // std::cout << "Calling Notify" << std::endl;
            dfu_server.notify(NORDIC_SECURE_DFU_SERVICE, NORDIC_DFU_CONTROL_POINT_CHAR,
                              std::string(reinterpret_cast<const char*>(data), length));
        });

        dfu_server.run_dfu();
        ble.disconnect();
        ble.dispose();

        if (dfu_server.get_state() == NativeDFU::DFU_FINISHED) {
            std::cout << "DFU Successful" << std::endl;
        } else {
            std::cout << "DFU Not Successful finished with state: 0x" << dfu_server.get_state() << std::endl;
        }
    }
    return true;
}

/**
 * main
 *
 * Test bench for DFU.
 * Usage: dfu_tester.exe <ble_address> <dfu_zip_path>
 *      -ble_address: Device BLE address in format compatible with BLE library
 *      -dfu_zip_file_path: Path to the DFU zip package
 *
 * Example usage:
 * .\bin\windows-x64\dfu_tester.exe EE4200000000 ./bin/vxx_y.zip
 * ./bin/linux/dfu_tester EE:42:00:00:00:00 ./bin/vxx_y.zip
 *
 */
int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cout << "Usage: " << argv[0] << " <ble_address> <dfu_zip_path>" << std::endl;
        return -1;
    }

    std::string device_dfu_ble_address(argv[1]);
    char* dfu_zip_filepath = argv[2];

    std::cout << "Starting DFU Test!" << std::endl;
    std::cout << "Initiating scan for " << SCAN_DURATION_MS << " milliseconds..." << std::endl;

    std::string data_file;
    std::string bin_file;

    if (!get_bin_dat_files(bin_file, data_file, dfu_zip_filepath)) {
        std::cout << "Could not parse DFU zip file!" << std::endl;
        return -1;
    }

    if (!data_file.length() || !bin_file.length()) {
        std::cout << "Empty Files" << std::endl;
        return -1;
    }

    std::cout << "Data file size: " << data_file.length() << std::endl;
    std::cout << "Bin file size: " << bin_file.length() << std::endl;

    if (!validate_mac_address(device_dfu_ble_address)) {
        std::cout << "Invalid MAC address supplied. Address must be at least 4 characters." << std::endl;
        return -1;
    }
    bool is_success = do_dfu_trigger(DFU_TARGET_NAME, device_dfu_ble_address);
    if (!is_success) {
        std::cout << "Trigger failed" << std::endl;
        return -1;
    }
    is_success = do_dfu(DFU_TARGET_NAME, data_file, bin_file);
    if (!is_success) {
        std::cout << "dfu failed." << std::endl;
        return -1;
    }
    return 0;
}

// Reads the manifest.json file to retrieve .bin and .dat files from DFU package.
bool get_bin_dat_files(std::string& bin, std::string& dat, const char* dfu_zip_path) {
    mz_zip_archive* zip_archive = new mz_zip_archive;
    char* manifest_file;
    nlohmann::json json_manifest;
    std::string bin_filename;
    size_t bin_size;
    char* bin_contents;
    std::string dat_filename;
    size_t dat_size;
    char* dat_contents;

    mz_zip_zero_struct(zip_archive);
    mz_zip_reader_init_file(zip_archive, dfu_zip_path, 0);

    manifest_file = (char*)mz_zip_reader_extract_file_to_heap(zip_archive, "manifest.json", (size_t*)NULL,
                                                              (mz_uint)NULL);
    if (!manifest_file) {
        return false;
    }

    json_manifest = nlohmann::json::parse(manifest_file);

    bin_filename = json_manifest["manifest"]["application"]["bin_file"];
    dat_filename = json_manifest["manifest"]["application"]["dat_file"];

    dat_contents = (char*)mz_zip_reader_extract_file_to_heap(zip_archive, dat_filename.c_str(), &dat_size,
                                                             (mz_uint)NULL);
    bin_contents = (char*)mz_zip_reader_extract_file_to_heap(zip_archive, bin_filename.c_str(), &bin_size,
                                                             (mz_uint)NULL);
    if (!dat_contents || !bin_contents) {
        return false;
    }

    bin = std::string(bin_contents, bin_size);
    dat = std::string(dat_contents, dat_size);

    zip_archive->m_pFree(zip_archive->m_pAlloc_opaque, manifest_file);
    zip_archive->m_pFree(zip_archive->m_pAlloc_opaque, dat_contents);
    zip_archive->m_pFree(zip_archive->m_pAlloc_opaque, bin_contents);
    delete zip_archive;

    return true;
}