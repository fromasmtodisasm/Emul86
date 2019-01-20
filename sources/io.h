#pragma once
#include "Disk.h"
#include "common.h"
#include <vector>
#include <list>
#include <chrono>

class CPU;

class IO
{
public:
	enum IN_OUT_SIZE
	{
		BYTE,
		WORD,
	};

	void Init();
	void DrawScreen(int m_displayScale);
	void ActivateVideoMode(int mode);

	// Keyboard
	std::pair<bool, word> UpdateKeyState();
	void KeyboardHalt();
	bool ISKeyboardHalted() const;
	bool SetCurrentKey(int c);
	void PushKey(int c, bool release);
	void SetKeyFlags(bool rightShift, bool leftShift, bool CTRL, bool alt);
	int GetCurrentKeyCode() const;

	// Disk
	void AddDisk(Disk disk);
	void ClearDisks();
	int GetDiskCount() const;
	bool HasDisk(int diskNo) const;
	bool ReadDisk(int diskNo, uint32_t ram_address, word cylinder, byte head, byte sector, uint32_t sectorCount);
	bool WriteDisk(int diskNo, uint32_t ram_address, word cylinder, byte head, byte sector, uint32_t sectorCount);
	const Disk::BIOS_ParameterBlock& GetDiskBPB(int diskNo) const;
	int GetBootableDisk() const;

	// Memory/Ports
	void ClearMemory();
	void MemStore(word offset, word location, byte* x, int size);
	void MemStoreB(word offset, word location, byte x);
	void MemStoreW(word offset, word location, word x);
	void MemStoreD(word offset, word location, uint32_t x);
	void MemStoreString(word offset, word location, const char* x);

	byte MemGetB(word offset, word location) const;
	word MemGetW(word offset, word location) const;
	uint32_t MemGetD(word offset, word location) const;

	template<typename T>
	void SetMemory(uint32_t address, T x);

	template<typename T>
	T GetMemory(uint32_t address) const;

	template<typename T>
	inline T GetPort(uint32_t address);

	template<typename T>
	inline void SetPort(uint32_t address, T x);

	word GetPort(uint32_t address, IN_OUT_SIZE size);
	void SetPort(uint32_t address, word x, IN_OUT_SIZE size);

	byte* GetRamRawPointer();
	
	// A20 gate
	void EnableA20Gate();
	void DisableA20Gate();
	bool GetA20GateStatus() const;

	void PITSignal();

	void AddCpu(CPU& cpu);

	void GUI();

	bool IsCustomIntHandlerInstalled(int x);
private:
	// Keyboard
	bool HasKeyToPop() const;
	int PopKey();
	void ClearCurrentCode();

	void BlitData(int displayScale);

	std::vector<Disk> m_floppyDrives;
	std::vector<Disk> m_hardDrives;
	std::list<int> keyBuffer;
	std::list<byte> scanCodeBuffer;

	byte* m_ram;
	byte* m_port;
	CPU* m_cpu;

	int A20;
	int m_currentKeyCode;
	int m_keyFlags;
	bool m_int16_halt;

	unsigned int m_texture;
	unsigned int m_fbo;

	struct DAC_entry
	{
		byte r, g, b;
	};

	struct DAC_controll
	{
		byte wreg;
		byte wcycle;
		byte rreg;
		byte rcycle;
		byte state;

		DAC_entry data[256];
		byte mask;
	};
		
	struct PIT
	{
		word current_count;
		word reload_val;
		byte mode;
	};

	PIT m_pit[3];

	DAC_controll m_dac;

	std::chrono::time_point<std::chrono::system_clock> m_start;
};

//#define ENABLE_A20

#ifdef ENABLE_A20
#define A20_GATE(X) (X & A20)
#else
#define A20_GATE(X) (X & 0xFFFFF)
#endif


template<>
inline void IO::SetMemory<byte>(uint32_t address, byte x)
{
	m_ram[A20_GATE(address)] = x;
}

template<>
inline byte IO::GetMemory<byte>(uint32_t address) const
{
	return m_ram[A20_GATE(address)];
}

template<>
inline void IO::SetMemory<word>(uint32_t address, word x)
{
	m_ram[A20_GATE(address)] = x & 0xFF;
	m_ram[A20_GATE(address) + 1] = (x & 0xFF00) >> 8;
}

template<>
inline word IO::GetMemory<word>(uint32_t address) const
{
	return ((word)m_ram[A20_GATE(address)]) | (((word)m_ram[A20_GATE(address) + 1]) << 8);
}

template<>
inline byte IO::GetPort<byte>(uint32_t address)
{
	return GetPort(address, BYTE);
}

template<>
inline word IO::GetPort<word>(uint32_t address)
{
	return GetPort(address, WORD);
}

template<>
inline void IO::SetPort<byte>(uint32_t address, byte x)
{
	SetPort(address, x, BYTE);
}

template<>
inline void IO::SetPort<word>(uint32_t address, word x)
{
	SetPort(address, x, WORD);
}
