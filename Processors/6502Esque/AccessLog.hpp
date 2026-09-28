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
//    bit 3  written after it had been executed or read; that write cleared
//           bits 0, 1 and 4 and the readers, so they cover only the uses of
//           the last value written (the one in the memory dump)
//    bit 4  reached auxiliary RAM rather than main (Apple IIe), so what the
//           CPU saw there may not be the main-memory byte
//  Addresses are as the CPU sees them, so bank switching is otherwise not
//  distinguished.
//
//  CLK_READERS names another: for each address, up to 4 little-endian 16-bit
//  addresses of instructions that read it other than as an opcode (0: none),
//  so a copy or checksum loop that reads everything can be told from a table
//  lookup.
//
//  CLK_MEMORY_DUMP names a second file: 65,536 bytes, the last value the CPU
//  read or wrote at each address (0 where it never touched one). For programs
//  loaded into RAM, that is the program as it ran.
//

#pragma once

#include "6502Esque.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace CPU::MOS6502Esque {

struct AccessLog {
	static constexpr uint8_t Executed = 0x01;
	static constexpr uint8_t Read = 0x02;
	static constexpr uint8_t Written = 0x04;
	static constexpr uint8_t ChangedAfterUse = 0x08;
	static constexpr uint8_t Aux = 0x10;

	std::array<uint8_t, 65536> flags{};
	std::array<uint8_t, 65536> memory{};
	std::array<std::array<uint16_t, 4>, 65536> readers{};
	uint16_t instruction = 0;	// where the instruction now running began
	std::atomic<uint64_t> cycles{0};	// bus cycles so far: emulated time
	uint64_t cycle_limit = 0;	// if set, nothing after this many cycles is recorded
	std::string path, memory_path, readers_path;
	bool enabled = false;

	AccessLog() {
		if(const char *const name = std::getenv("CLK_ACCESS_LOG"); name && *name) {
			path = name;
			enabled = true;
		}
		if(const char *const name = std::getenv("CLK_MEMORY_DUMP"); name && *name) {
			memory_path = name;
			enabled = true;
		}
		if(const char *const name = std::getenv("CLK_READERS"); name && *name) {
			readers_path = name;
			enabled = true;
		}
	}

	~AccessLog() {
		write();
	}

	/// Called after each bus operation, with the value read or written.
	void record(const BusOperation operation, const uint16_t address, const uint8_t *const value) {
		if(cycles.fetch_add(1, std::memory_order_relaxed) >= cycle_limit && cycle_limit) return;
		switch(operation) {
			case BusOperation::ReadOpcode:
				flags[address] |= Executed;
				instruction = address;
				break;
			case BusOperation::Read:
			case BusOperation::ReadProgram:
			case BusOperation::ReadVector: {
				flags[address] |= Read;
				auto &slots = readers[address];
				for(auto &slot : slots) {
					if(slot == instruction) break;
					if(!slot) { slot = instruction; break; }
				}
			} break;
			case BusOperation::Write:
				// Uses before this write were of another value: forget them, so the
				// flags and readers describe the value that ends up in the dump.
				if(flags[address] & (Executed | Read)) {
					flags[address] = ChangedAfterUse;
					readers[address] = {};
				}
				flags[address] |= Written;
				break;
			default: return;
		}
		if(value) memory[address] = *value;
	}

	/// Writes the log now; also called on exit.
	void write() const {
		if(!enabled) return;
		if(path.empty()) {
		} else if(FILE *const file = std::fopen(path.c_str(), "wb")) {
			std::fwrite(flags.data(), 1, flags.size(), file);
			std::fclose(file);
		}
		if(readers_path.empty()) {
		} else if(FILE *const file = std::fopen(readers_path.c_str(), "wb")) {
			for(const auto &slots : readers) {
				for(const auto slot : slots) {
					const uint8_t bytes[2] = {uint8_t(slot), uint8_t(slot >> 8)};
					std::fwrite(bytes, 1, 2, file);
				}
			}
			std::fclose(file);
		}
		if(memory_path.empty()) {
		} else if(FILE *const file = std::fopen(memory_path.c_str(), "wb")) {
			std::fwrite(memory.data(), 1, memory.size(), file);
			std::fclose(file);
		}
	}
};

/// The one log shared by every 6502-family CPU in the process.
inline AccessLog access_log;

}
