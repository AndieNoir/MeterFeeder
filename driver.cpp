/**
 * MeterFeeder Library
 * 
 * by fp2.dev
 */

#include "driver.h"

bool MeterFeeder::Driver::Initialize(string* errorReason) {
	DWORD numDevices;
	FT_STATUS ftdiStatus = FT_CreateDeviceInfoList(&numDevices);
	if (ftdiStatus != FT_OK || numDevices < 1) {
		makeErrorStr(errorReason, "Error creating device info list. Check if generators are connected.");
		return false;
	}

	vector<FT_DEVICE_LIST_INFO_NODE> devInfoList(numDevices);
	ftdiStatus = FT_GetDeviceInfoList(&devInfoList[0], &numDevices);
	if (ftdiStatus != FT_OK) {
		makeErrorStr(errorReason, "Error getting the device info list");
		return false;
	}

	// Open devices by serialNumber
	for (int i = 0; i < numDevices; i++) {
		string serialNumber = devInfoList[i].SerialNumber;
		serialNumber.resize(sizeof(devInfoList[i].SerialNumber));
		FT_HANDLE ftHandle = devInfoList[i].ftHandle;

		if (serialNumber.find("QWR4") != 0) {
			// Skip any other but MED1K or MED100K devices
			continue;
		}

		// Open the current device
		ftdiStatus = FT_OpenEx(&devInfoList[i].SerialNumber, FT_OPEN_BY_SERIAL_NUMBER, &ftHandle);
		if (ftdiStatus != FT_OK) {
			makeErrorStr(errorReason, "Failed to connect to %s", &serialNumber);
			return false;
		}

		// Configure FTDI transport parameters
		FT_SetLatencyTimer(ftHandle, FTDI_DEVICE_LATENCY_MS);
		if (ftdiStatus != FT_OK) {
			makeErrorStr(errorReason, "Failed to set latency time for %s", &serialNumber);
			return false;
		}
		FT_SetUSBParameters(ftHandle, FTDI_DEVICE_PACKET_USB_SIZE, FTDI_DEVICE_PACKET_USB_SIZE);
		if (ftdiStatus != FT_OK) {
			makeErrorStr(errorReason, "Failed to set in/out packset size for %s", &serialNumber);
			return false;
		}
		FT_SetTimeouts(ftHandle, FTDI_DEVICE_TX_TIMEOUT_MS, FTDI_DEVICE_TX_TIMEOUT_MS);
		if (ftdiStatus != FT_OK) {
			makeErrorStr(errorReason, "Failed to set timeout time for %s", &serialNumber);
			return false;
		}

		// Device is successfully initialized. Add it to the list of generators the driver will control.
		Generator generator = Generator(&devInfoList[i].SerialNumber[0], &devInfoList[i].Description[0], ftHandle);
		_generators.push_back(generator);
	}

	return true;
};

void MeterFeeder::Driver::Shutdown() {
	// Shutdown all generators
	for (int i = 0; i < _generators.size(); i++) {
		_generators[i].Close();
	}
};

int MeterFeeder::Driver::GetNumberGenerators() {
	return _generators.size();	
};

vector<MeterFeeder::Generator>* MeterFeeder::Driver::GetListGenerators() {
	return &_generators;
};

void MeterFeeder::Driver::GetByte(FT_HANDLE handle, unsigned char* entropyByte, string* errorReason) {
	// Find the specified generator
	Generator *generator = FindGeneratorByHandle(handle);
	if (!generator) {
		makeErrorStr(errorReason, "Could not find %s by the handle %x", generator->GetSerialNumber().c_str(), generator->GetHandle());
		return;
	}

	// Get the device to start measuring randomness
	if (MF_DEVICE_ERROR == generator->Stream()) {
		makeErrorStr(errorReason, "Error instructing %s to start streaming entropy", generator->GetSerialNumber().c_str());
		return;
	}

	// Read in the entropy
	if (MF_DEVICE_ERROR == generator->Read(entropyByte)) {
		makeErrorStr(errorReason, "Error reading in entropy from %s", generator->GetSerialNumber().c_str());
		return;
	}
};

MeterFeeder::Generator* MeterFeeder::Driver::FindGeneratorByHandle(FT_HANDLE handle) {
	for (int i = 0; i < _generators.size(); i++) {
		if (_generators[i].GetHandle() == handle) {
			return &_generators[i];
		}
	}

	return nullptr;
};

MeterFeeder::Generator* MeterFeeder::Driver::FindGeneratorBySerial(string serialNumber) {
	for (int i = 0; i < _generators.size(); i++) {
		if (_generators[i].GetSerialNumber() == serialNumber) {
			return &_generators[i];
		}
	}

	return nullptr;
};

void MeterFeeder::Driver::makeErrorStr(string* errorReason, const char* format, ...) {
	char buffer[MF_ERROR_STR_MAX_LEN];
	va_list args;
	va_start (args, format);
	vsnprintf (buffer, MF_ERROR_STR_MAX_LEN-1, format, args);
	*errorReason = buffer;
	va_end (args);
};

/**
 * Code below for interfacing with MeterFeeder as a library
 */

// #ifdef APPLE
	#define DllExport __attribute__((visibility("default")))
// #else
// 	#define DllExport _declspec (dllexport)
// #endif

extern "C" {
	/**
	 * Library functions exposed for consumption by Unity/C# or whomever
	 */

	using namespace MeterFeeder;
	Driver driver = Driver();

	// Initialize the connected generators
	DllExport int Initialize(char *pErrorReason) {
		string errorReason = "";
		int res = driver.Initialize(&errorReason);
		std::strcpy(pErrorReason, errorReason.c_str());
		return res;
	}

	// Shutdown and de-initialize all the generators.
    DllExport void Shutdown() {
		driver.Shutdown();
	}

    // Get the number of connected and successfully initialized generators.
	DllExport int GetNumberGenerators() {
		return driver.GetNumberGenerators();
	}

  	/// Get the list of connected and successfully initialized generators.
	DllExport vector<Generator>* GetListGenerators() {
		return nullptr; // TODO: implement
	}

	// Get a byte of randomness.
	DllExport unsigned char GetByte(char* generatorSerialNumber, char* pErrorReason) {
		string errorReason = "";
		Generator *generator = driver.FindGeneratorBySerial(generatorSerialNumber);
		unsigned char byte = 1;
		driver.GetByte(generator->GetHandle(), &byte, &errorReason);
		std::strcpy(pErrorReason, errorReason.c_str());
		return byte;
	}
}