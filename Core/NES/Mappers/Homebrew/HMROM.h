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
	uint16_t _chrBanks[16];
	uint8_t _ntMirrVal; // only used for state display
	bool _ramBank; // only used for state display

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
			_chrBanks[i] = (GetPowerOnByte()) | ((GetPowerOnByte() & 0x0F) << 8);
				(i, _chrBanks[i] | (i >= 8 ? 0x200000 : 0x000000));
		}
		_ntMirrVal = GetPowerOnByte() & 0x03;
		if(GetMirroringType() != MirroringType::FourScreens) {
			switch(_ntMirrVal) {
				case 0: SetMirroringType(MirroringType::Vertical); break;
				case 1: SetMirroringType(MirroringType::Horizontal); break;
				case 2: SetMirroringType(MirroringType::ScreenAOnly); break;
				case 3: SetMirroringType(MirroringType::ScreenBOnly); break;
			}
		}
		_ramBank = GetPowerOnByte() & 0x01;
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
		SV(_ramBank);
		SV(_inhibitIrq);
		SV(_irqCounter);
		SV(_ppuA12prev);
	}

	void ProcessCpuClock() override
	{
		BaseProcessCpuClock();
		if(_romInfo.SubMapperID & 0x01) {
			_audio2->Clock();
			_audio2->Clock();
		} else {
			_audio->Clock();
		}
	}

	void WriteRegister(uint16_t addr, uint8_t value) override
	{
		switch(addr & 0xF000) {
			case 0x8000: // PRG-ROM bank select
				_prgBank = value;
				SelectPrgPage(0, value);
				break;
			case 0x9000: // CHR bank select
				_chrBanks[(addr >> 4) & 0x0F] = value | ((addr & 0x0F) << 8);
				SelectChrPage((addr >> 4) & 0x0F, (value | ((addr & 0x0F) << 8)) | (((addr >> 4) & 0x0F) >= 8 ? 0x200000 : 0x000000));
				break;
			case 0xA000: // Board settings
				_inhibitIrq = value >> 7;
				UpdateIrqStatus();
				if(GetMirroringType() != MirroringType::FourScreens) {
					switch(value & 0x03) {
						case 0: SetMirroringType(MirroringType::Vertical); break;
						case 1: SetMirroringType(MirroringType::Horizontal); break;
						case 2: SetMirroringType(MirroringType::ScreenAOnly); break;
						case 3: SetMirroringType(MirroringType::ScreenBOnly); break;
					}
				}
				_ramBank = (value >> 6) & 0x01;
				SetCpuMemoryMapping(0x6000, 0x7FFF, _ramBank, HasBattery() ? PrgMemoryType::SaveRam : PrgMemoryType::WorkRam, MemoryAccessType::ReadWrite);
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
		switch(_ntMirrVal) {
			case 0:
				mirroringType = "Vertical";
				break;
			case 1:
				mirroringType = "Horizontal";
				break;
			case 2:
				mirroringType = "Screen A";
				break;
			case 3:
				mirroringType = "Screen B";
				break;
		}

		entries.push_back(MapperStateEntry("$8000", "PRG Bank", _prgBank, MapperStateValueType::Number8));

		entries.push_back(MapperStateEntry("$900x", "CHR Bank 0", _chrBanks[0], MapperStateValueType::Number16));
		entries.push_back(MapperStateEntry("$901x", "CHR Bank 1", _chrBanks[1], MapperStateValueType::Number16));
		entries.push_back(MapperStateEntry("$902x", "CHR Bank 2", _chrBanks[2], MapperStateValueType::Number16));
		entries.push_back(MapperStateEntry("$903x", "CHR Bank 3", _chrBanks[3], MapperStateValueType::Number16));
		entries.push_back(MapperStateEntry("$904x", "CHR Bank 4", _chrBanks[4], MapperStateValueType::Number16));
		entries.push_back(MapperStateEntry("$905x", "CHR Bank 5", _chrBanks[5], MapperStateValueType::Number16));
		entries.push_back(MapperStateEntry("$906x", "CHR Bank 6", _chrBanks[6], MapperStateValueType::Number16));
		entries.push_back(MapperStateEntry("$907x", "CHR Bank 7", _chrBanks[7], MapperStateValueType::Number16));
		entries.push_back(MapperStateEntry("$908x", "CHR Bank 8", _chrBanks[8], MapperStateValueType::Number16));
		entries.push_back(MapperStateEntry("$909x", "CHR Bank 9", _chrBanks[9], MapperStateValueType::Number16));
		entries.push_back(MapperStateEntry("$90Ax", "CHR Bank 10", _chrBanks[10], MapperStateValueType::Number16));
		entries.push_back(MapperStateEntry("$90Bx", "CHR Bank 11", _chrBanks[11], MapperStateValueType::Number16));
		entries.push_back(MapperStateEntry("$90Cx", "CHR Bank 12", _chrBanks[12], MapperStateValueType::Number16));
		entries.push_back(MapperStateEntry("$90Dx", "CHR Bank 13", _chrBanks[13], MapperStateValueType::Number16));
		entries.push_back(MapperStateEntry("$90Ex", "CHR Bank 14", _chrBanks[14], MapperStateValueType::Number16));
		entries.push_back(MapperStateEntry("$90Fx", "CHR Bank 15", _chrBanks[15], MapperStateValueType::Number16));

		entries.push_back(MapperStateEntry("$A000.0-3", "Mirroring", mirroringType, _ntMirrVal));
		entries.push_back(MapperStateEntry("$A000.6", "RAM Bank", _ramBank, MapperStateValueType::Bool));
		entries.push_back(MapperStateEntry("$A000.7", "IRQ Inhibited", _inhibitIrq, MapperStateValueType::Bool));

		entries.push_back(MapperStateEntry("$B000", "IRQ Counter Value", _irqCounter & 0xFFF, MapperStateValueType::Number16));

		return entries;
	}
};