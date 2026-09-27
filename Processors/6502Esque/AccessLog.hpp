//
//  AccessLog.hpp
//  Clock Signal
//
//  Records which addresses a 6502-family CPU executed, read and wrote, for
//  tools that need to know what in a ROM is code and what is data.
//
//  Off unless the environment variable CLK_ACCESS_LOG names an output file.
//  On exit it writes 65,536 bytes, one per CPU address, each a set of flags:
//    bit 0  an instruction was fetched here (SYNC / ReadOpcode)
//    bit 1  read in any other way (operands, data, dummy reads)
//    bit 2  written
//  Addresses are as the CPU sees them, so bank switching is not distinguished.
//

#pragma once

#include "6502Esque.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace CPU::MOS6502Esque {

struct AccessLog {
	static constexpr uint8_t Executed = 0x01;
	static constexpr uint8_t Read = 0x02;
	static constexpr uint8_t Written = 0x04;

	std::array<uint8_t, 65536> flags{};
	std::string path;
	bool enabled = false;

	AccessLog() {
		if(const char *const name = std::getenv("CLK_ACCESS_LOG"); name && *name) {
			path = name;
			enabled = true;
		}
	}

	~AccessLog() {
		write();
	}

	void record(const BusOperation operation, const uint16_t address) {
		switch(operation) {
			case BusOperation::ReadOpcode:	flags[address] |= Executed;	break;
			case BusOperation::Read:
			case BusOperation::ReadProgram:
			case BusOperation::ReadVector:	flags[address] |= Read;		break;
			case BusOperation::Write:		flags[address] |= Written;	break;
			default: break;
		}
	}

	/// Writes the log now; also called on exit.
	void write() const {
		if(!enabled) return;
		if(FILE *const file = std::fopen(path.c_str(), "wb")) {
			std::fwrite(flags.data(), 1, flags.size(), file);
			std::fclose(file);
		}
	}
};

/// The one log shared by every 6502-family CPU in the process.
inline AccessLog access_log;

}
