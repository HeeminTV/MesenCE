#pragma once
#include "pch.h"
#include "NES/BaseMapper.h"

class HMROM : public BaseMapper
{
private:
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

	void InitMapper() override
	{
		_prgBank = GetPowerOnByte();
		SelectPrgPage(0, _prgBank);

		for(char i = 0; i <= 15; i++) {
			_chrBanks[i] = GetPowerOnByte();
		}
		_chrOuter = GetPowerOnByte() & 0x07;
		_ramBank = GetPowerOnByte() & 0x03;
		SetCpuMemoryMapping(0x6000, 0x7FFF, _ramBank, HasBattery() ? PrgMemoryType::SaveRam : PrgMemoryType::WorkRam, MemoryAccessType::ReadWrite);
		_irqCounter = GetPowerOnByte() | ((GetPowerOnByte() << 8) & 0x0F);
		_inhibitIrq = GetPowerOnByte() & 0x01;
		UpdateChrStatus();
	}

	void Reset(bool softReset) override
	{
	}

	void Serialize(Serializer& s) override
	{
		BaseMapper::Serialize(s);
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
				_inhibitIrq == value >> 7;
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
			case 0xB000: // YM2413 / AY-3-8910 port ($B000-$BFFF)
				break;
			case 0xC000: // IRQ scanline counter value
				_irqCounter = (value << 3) | 0x807;
				UpdateIrqStatus();
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
		if(!_ppuA12prev && (addr & 0x1000) == 0x1000) {
			// PPU A12 rising edge
			_irqCounter--;
			_irqCounter &= 0xFFF;
			UpdateIrqStatus();
		}
		_ppuA12prev = (addr & 0x1000) == 0x1000;
	}
};