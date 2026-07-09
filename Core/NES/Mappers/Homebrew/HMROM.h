#pragma once
#include "pch.h"
#include "NES/BaseMapper.h"
#include "NES/Mappers/Audio/HmRomAudio.h"
#include "NES/Mappers/Audio/Sunsoft5bAudio.h"

class HMROM : public BaseMapper
{
private:
	unique_ptr<HmRomAudio> _audio;
	unique_ptr<Sunsoft5bAudio> _audio2;

	uint8_t _prgBank; // only used for state display
	uint8_t _chrBanks[16];
	uint8_t _chrOuter;
	uint8_t _ramBank; // only used for state display

	bool _inhibitIrq;
	uint16_t _irqCounter; // actually 12-bit
	bool _ppuA12prev;

protected:
	uint16_t GetPrgPageSize() override { return 0x8000; }
	uint16_t GetChrPageSize() override { return 0x200; }
	uint32_t GetWorkRamSize() override { return 0x8000; }
	uint32_t GetWorkRamPageSize() override { return 0x2000; }
	uint32_t GetSaveRamSize() override { return 0x8000; }
	uint32_t GetSaveRamPageSize() override { return 0x2000; }
	bool EnableCpuClockHook() override { return true; }
	bool EnableVramAddressHook() override { return true; }

	void InitMapper() override
	{
		_prgBank = GetPowerOnByte();
		SelectPrgPage(0, _prgBank);
		for(char i = 0; i <= 15; i++) {
			_chrBanks[i] = GetPowerOnByte();
		}
		_chrOuter = GetPowerOnByte() & 0x07;
		UpdateChrStatus();
		_ramBank = GetPowerOnByte() & 0x03;
		SetCpuMemoryMapping(0x6000, 0x7FFF, _ramBank, HasBattery() ? PrgMemoryType::SaveRam : PrgMemoryType::WorkRam, MemoryAccessType::ReadWrite);
		_inhibitIrq = GetPowerOnByte() & 0x01;
		_irqCounter = GetPowerOnByte() | ((GetPowerOnByte() << 8) & 0xF00);
		_ppuA12prev = GetPowerOnByte() & 0x01;

		_audio.reset(new HmRomAudio(_console));
		_audio2.reset(new Sunsoft5bAudio(_console));
	}

	void Reset(bool softReset) override
	{
		_audio->Reset();
	}

	void Serialize(Serializer& s) override
	{
		BaseMapper::Serialize(s);
		SV(_audio);
		SV(_audio2);

		SV(_prgBank);
		SVArray(_chrBanks, 16);
		SV(_chrOuter);
		SV(_ramBank);
		SV(_inhibitIrq);
		SV(_irqCounter);
		SV(_ppuA12prev);
	}

	void ProcessCpuClock() override
	{
		BaseProcessCpuClock();
		_audio->Clock();
		_audio2->Clock();
		_audio2->Clock();
	}

	void WriteRegister(uint16_t addr, uint8_t value) override
	{
		switch(addr & 0xF000) {
			case 0x8000: // PRG-ROM bank select
				_prgBank = value;
				SelectPrgPage(0, value);
				break;
			case 0x9000: // CHR bank select
				_chrBanks[addr & 0x0F] = value;
				UpdateChrStatus();
				break;
			case 0xA000: // Board settings
				_inhibitIrq = value >> 7;
				UpdateIrqStatus();
				switch((value >> 5) & 0x03) {
					case 0: SetMirroringType(MirroringType::Vertical); break;
					case 1: SetMirroringType(MirroringType::Horizontal); break;
					case 2: SetMirroringType(MirroringType::ScreenAOnly); break;
					case 3: SetMirroringType(MirroringType::ScreenBOnly); break;
				}
				_ramBank = (value >> 3) & 0x03;
				SetCpuMemoryMapping(0x6000, 0x7FFF, (value >> 3) & 0x03, HasBattery() ? PrgMemoryType::SaveRam : PrgMemoryType::WorkRam, MemoryAccessType::ReadWrite);
				_chrOuter = value & 0x07;
				UpdateChrStatus();
				break;
			case 0xB000: // IRQ scanline counter value
				_irqCounter = (value << 3) | 0x807;
				UpdateIrqStatus();
				break;
			case 0xC000: // YM2413 / AY-3-8910 port ($B000-$BFFF)
				if(_romInfo.SubMapperID & 0x01) {
					_audio2->WriteRegister(addr & 0x01 ? 0xE000 : 0xC000, value);
				} else {
					_audio->WriteOPLL(addr & 0x01, value);
				}
				break;
		}
	}

	inline void UpdateChrStatus()
	{
		for(int i = 0; i <= 15; i++) {
			SelectChrPage(i, _chrBanks[i] | (_chrOuter << 9) | (i >= 8 ? 0x100 : 0x000));
		}
	}

	inline void UpdateIrqStatus()
	{
		if((_irqCounter & 0x800) == 0 && !_inhibitIrq) {
			_console->GetCpu()->SetIrqSource(IRQSource::External);
		} else {
			_console->GetCpu()->ClearIrqSource(IRQSource::External);
		}
	}

	void NotifyVramAddressChange(uint16_t addr) override
	{
		bool PPU_A12 = (addr & 0x1000) == 0x1000;
		if(!_ppuA12prev && PPU_A12) {
			// PPU A12 rising edge
			_irqCounter--;
			// _irqCounter &= 0xFFF;
			UpdateIrqStatus();
		}
		_ppuA12prev = PPU_A12;
	}

	vector<MapperStateEntry> GetMapperStateEntries() override
	{
		vector<MapperStateEntry> entries;
		string mirroringType;
		uint8_t mirValue = 0;
		switch(GetMirroringType()) {
			case MirroringType::Vertical:
				mirroringType = "Vertical";
				mirValue = 0;
				break;
			case MirroringType::Horizontal:
				mirroringType = "Horizontal";
				mirValue = 1;
				break;
			case MirroringType::ScreenAOnly:
				mirroringType = "Screen A";
				mirValue = 2;
				break;
			case MirroringType::ScreenBOnly:
				mirroringType = "Screen B";
				mirValue = 3;
				break;
		}

		entries.push_back(MapperStateEntry("$8000", "PRG Bank", _prgBank, MapperStateValueType::Number8));

		entries.push_back(MapperStateEntry("$9000", "CHR Bank 0", _chrBanks[0], MapperStateValueType::Number8));
		entries.push_back(MapperStateEntry("$9001", "CHR Bank 1", _chrBanks[1], MapperStateValueType::Number8));
		entries.push_back(MapperStateEntry("$9002", "CHR Bank 2", _chrBanks[2], MapperStateValueType::Number8));
		entries.push_back(MapperStateEntry("$9003", "CHR Bank 3", _chrBanks[3], MapperStateValueType::Number8));
		entries.push_back(MapperStateEntry("$9004", "CHR Bank 4", _chrBanks[4], MapperStateValueType::Number8));
		entries.push_back(MapperStateEntry("$9005", "CHR Bank 5", _chrBanks[5], MapperStateValueType::Number8));
		entries.push_back(MapperStateEntry("$9006", "CHR Bank 6", _chrBanks[6], MapperStateValueType::Number8));
		entries.push_back(MapperStateEntry("$9007", "CHR Bank 7", _chrBanks[7], MapperStateValueType::Number8));
		entries.push_back(MapperStateEntry("$9008", "CHR Bank 8", _chrBanks[8], MapperStateValueType::Number8));
		entries.push_back(MapperStateEntry("$9009", "CHR Bank 9", _chrBanks[9], MapperStateValueType::Number8));
		entries.push_back(MapperStateEntry("$900A", "CHR Bank 10", _chrBanks[10], MapperStateValueType::Number8));
		entries.push_back(MapperStateEntry("$900B", "CHR Bank 11", _chrBanks[11], MapperStateValueType::Number8));
		entries.push_back(MapperStateEntry("$900C", "CHR Bank 12", _chrBanks[12], MapperStateValueType::Number8));
		entries.push_back(MapperStateEntry("$900D", "CHR Bank 13", _chrBanks[13], MapperStateValueType::Number8));
		entries.push_back(MapperStateEntry("$900E", "CHR Bank 14", _chrBanks[14], MapperStateValueType::Number8));
		entries.push_back(MapperStateEntry("$900F", "CHR Bank 15", _chrBanks[15], MapperStateValueType::Number8));

		entries.push_back(MapperStateEntry("$A000.0-2", "CHR Outer Bank", _chrOuter, MapperStateValueType::Number8));
		entries.push_back(MapperStateEntry("$A000.3-4", "RAM Bank", _ramBank, MapperStateValueType::Number8));
		entries.push_back(MapperStateEntry("$A000.5-6", "Mirroring", mirroringType, mirValue));
		entries.push_back(MapperStateEntry("$A000.7", "IRQ Inhibited", _inhibitIrq, MapperStateValueType::Bool));

		entries.push_back(MapperStateEntry("$B000", "IRQ Counter Value", _irqCounter, MapperStateValueType::Number16));

		return entries;
	}
};