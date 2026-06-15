#pragma once
#include "pch.h"
#include "NES/BaseMapper.h"
#include "NES/Mappers/Audio/HmRomAudio.h" // TODO: merge with VRC7Audio.h?

class HMROM : public BaseMapper
{
private:
	uint8_t _prgBank; // only used for state display
	uint8_t _bigBank;
	uint8_t _smallBanks[8];
	uint8_t _chrOuter;
	bool _ramBank; // only used for state display
	bool _twistedChr;

	unique_ptr<HmRomAudio> _audio;

protected:
	uint16_t GetPrgPageSize() override { return 0x8000; }
	uint16_t GetChrPageSize() override { return 0x200; }
	uint32_t GetWorkRamSize() override { return 0x6000; }
	uint32_t GetWorkRamPageSize() override { return 0x3000; }
	uint32_t GetSaveRamSize() override { return 0x6000; }
	uint32_t GetSaveRamPageSize() override { return 0x3000; }
	bool EnableCpuClockHook() override { return true; }

	void InitMapper() override
	{
		_prgBank = GetPowerOnByte();
		SelectPrgPage(0, _prgBank);

		_bigBank = GetPowerOnByte() & 0x1F;
		for(char i = 0; i <= 7; i++) {
			_smallBanks[i] = GetPowerOnByte();
		}
		_chrOuter = GetPowerOnByte() & 0x1F;
		_twistedChr = GetPowerOnByte() & 0x01;
		UpdateChrStatus();

		_ramBank = GetPowerOnByte() & 0x01;
		SetCpuMemoryMapping(0x5000, 0x7FFF, _ramBank, HasBattery() ? PrgMemoryType::SaveRam : PrgMemoryType::WorkRam, MemoryAccessType::ReadWrite);

		_audio.reset(new HmRomAudio(_console));
	}

	void Reset(bool softReset) override
	{
		_audio->Reset();
	}

	void Serialize(Serializer& s) override
	{
		BaseMapper::Serialize(s);
		SV(_audio);
	}

	void ProcessCpuClock() override
	{
		BaseProcessCpuClock();
		_audio->Clock();
	}

	void WriteRegister(uint16_t addr, uint8_t value) override
	{
		switch(addr & 0xF000) {
			case 0x8000: // PRG-ROM bank select
				SelectPrgPage(0, value);
				_prgBank = value;
				break;
			case 0x9000: // CHR bank select 0
				_bigBank = value & 0x1F; 
				UpdateChrStatus();
				break;
			case 0xA000: // CHR bank select 1-8
				_smallBanks[addr & 0x07] = value; 
				UpdateChrStatus();
				break;
			case 0xB000: // Board settings
				switch(value >> 6) {
					case 0: SetMirroringType(MirroringType::Vertical); break;
					case 1: SetMirroringType(MirroringType::Horizontal); break;
					case 2: SetMirroringType(MirroringType::ScreenAOnly); break;
					case 3: SetMirroringType(MirroringType::ScreenBOnly); break;
				}
				_twistedChr = (value & 0x20) == 0x20;
				SetCpuMemoryMapping(0x5000, 0x7FFF, (value & 0x10) >> 4, HasBattery() ? PrgMemoryType::SaveRam : PrgMemoryType::WorkRam, MemoryAccessType::ReadWrite);
				_chrOuter = value & 0x0F;
				UpdateChrStatus();
				break;
			case 0xC000: // YM2413 port
				_audio->WriteOPLL((addr & 0x01) == 0x01, value);
				break;

			case 0xD000: // ATTiny85 communication output port
				_audio->WriteATtiny85(value);
				break;
		}
	}

	inline void UpdateChrStatus()
	{
		int _outerOr = _chrOuter << 9; // 256K outer
		if(_twistedChr == 0) { // linear mode
			for(int i = 0; i <= 7; i++) {
				SelectChrPage(i + 0, (((_bigBank & 0x1F) << 3) + i) | _outerOr);
				SelectChrPage(i + 8, _smallBanks[i] | _outerOr | 0x100);
			}
		} else { // twisted mode
			for(int i = 0; i <= 3; i++) {
				SelectChrPage(i + 0, (((_bigBank & 0x1F) << 3) + i + 0) | _outerOr);
				SelectChrPage(i + 4, _smallBanks[i + 0] | _outerOr | 0x100);
				SelectChrPage(i + 8, (((_bigBank & 0x1F) << 3) + i + 4) | _outerOr);
				SelectChrPage(i + 12, _smallBanks[i + 4] | _outerOr | 0x100);
			}
		}
	}

	vector<MapperStateEntry> GetMapperStateEntries() override
	{
		vector<MapperStateEntry> entries;
		string mirroringType;
		int64_t mirValue = 0;
		switch(GetMirroringType()) {
			case MirroringType::Horizontal:
				mirroringType = "Horizontal";
				mirValue = 3;
				break;
			case MirroringType::Vertical:
				mirroringType = "Vertical";
				mirValue = 2;
				break;
			case MirroringType::ScreenBOnly:
				mirroringType = "Screen B";
				mirValue = 1;
				break;
			case MirroringType::ScreenAOnly:
				mirroringType = "Screen A";
				mirValue = 0;
				break;
		}
		entries.push_back(MapperStateEntry("$8xxx", "PRG-ROM Bank", _prgBank, MapperStateValueType::Number8));
		entries.push_back(MapperStateEntry("$9xxx", "CHR Bank 0", _bigBank, MapperStateValueType::Number8));

		entries.push_back(MapperStateEntry("$Axx0", "CHR Bank 1", _smallBanks[0], MapperStateValueType::Number8));
		entries.push_back(MapperStateEntry("$Axx1", "CHR Bank 2", _smallBanks[1], MapperStateValueType::Number8));
		entries.push_back(MapperStateEntry("$Axx2", "CHR Bank 3", _smallBanks[2], MapperStateValueType::Number8));
		entries.push_back(MapperStateEntry("$Axx3", "CHR Bank 4", _smallBanks[3], MapperStateValueType::Number8));
		entries.push_back(MapperStateEntry("$Axx4", "CHR Bank 5", _smallBanks[4], MapperStateValueType::Number8));
		entries.push_back(MapperStateEntry("$Axx5", "CHR Bank 6", _smallBanks[5], MapperStateValueType::Number8));
		entries.push_back(MapperStateEntry("$Axx6", "CHR Bank 7", _smallBanks[6], MapperStateValueType::Number8));
		entries.push_back(MapperStateEntry("$Axx7", "CHR Bank 8", _smallBanks[7], MapperStateValueType::Number8));

		entries.push_back(MapperStateEntry("$Bxxx.6-7", "Mirroring", mirroringType));
		entries.push_back(MapperStateEntry("$Bxxx.5", "CHR Banking mode", _twistedChr));
		entries.push_back(MapperStateEntry("$Bxxx.4", "PRG-RAM Bank", _ramBank));
		entries.push_back(MapperStateEntry("$Bxxx.0-3", "CHR Outer Bank", _chrOuter, MapperStateValueType::Number8));

		entries.push_back(MapperStateEntry("$Cxxx", "YM2413 Current Register", _audio->getOPLLcurrentReg(), MapperStateValueType::Number8));

		entries.push_back(MapperStateEntry("$Dxxx.7", "ATtiny85 Port SCL", _audio->getA85SCL()));
		entries.push_back(MapperStateEntry("$Dxxx.0", "ATtiny85 Port SDA", _audio->getA85SDA()));
		entries.push_back(MapperStateEntry("$Dxxx.6", "ATtiny85 Port Address / Data", _audio->getA85A0()));

		entries.push_back(MapperStateEntry("", "ATtiny85 USIDR", _audio->getCurrentUSIDR(), MapperStateValueType::Number8));
		entries.push_back(MapperStateEntry("", "ATtiny85 USICNT0-3", _audio->getUSICounter(), MapperStateValueType::Number8));

		entries.push_back(MapperStateEntry("", "ATtiny85 Current Register", _audio->getA85Reg(), MapperStateValueType::Number8));

		entries.push_back(MapperStateEntry("", "ATtiny85 Internal Pointer", _audio->getA85ptr(), MapperStateValueType::Number16));
		entries.push_back(MapperStateEntry("", "ATtiny85 Current DAC Output", _audio->getA85DAC(), MapperStateValueType::Number8));
		entries.push_back(MapperStateEntry("", "ATtiny85 Playback Halt", _audio->getPCMHalt()));

		return entries;
	}
};