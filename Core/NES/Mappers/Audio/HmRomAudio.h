#pragma once
#include "pch.h"
#include "NES/APU/NesApu.h"
#include "NES/NesConsole.h"
#include "Shared/Utilities/emu2413.h"
#include "Shared/Utilities/Emu2413Serializer.h"
#include "Utilities/Serializer.h"
class HmRomAudio : public ISerializable
{
private:
	static constexpr int OpllSampleRate = 49716;
	static constexpr int OpllClockRate = HmRomAudio::OpllSampleRate * 72;

	NesConsole* _console = nullptr;
	NesApu* _apu = nullptr;

	OPLL* _opll = nullptr;
	uint8_t _currentReg = 0;
	int16_t _previousOutput = 0;
	double _clockTimer = 0;

protected:
	void Serialize(Serializer& s) override
	{
		SV(_currentReg);
		SV(_previousOutput);
		SV(_clockTimer);
		Emu2413Serializer::Serialize(_opll, s);
	}

public:
	__forceinline void Clock()
	{
		// Process YM2413
		if(!_apu->IsApuEnabled()) {
			return;
		}

		if(_clockTimer == 0) {
			_clockTimer = ((double)_console->GetMasterClockRate()) / HmRomAudio::OpllSampleRate;
		}

		_clockTimer--;
		if(_clockTimer <= 0) {
			int16_t output = OPLL_calc(_opll);
			_console->GetApu()->AddExpansionAudioDelta(AudioChannel::VRC7, output - _previousOutput);
			_previousOutput = output;
			_clockTimer = ((double)_console->GetMasterClockRate()) / HmRomAudio::OpllSampleRate;
		}
	}

	HmRomAudio(NesConsole* console)
	{
		_console = console;
		_apu = console->GetApu();

		_previousOutput = 0;
		_currentReg = 0;
		_clockTimer = 0;

		_opll = OPLL_new(HmRomAudio::OpllClockRate, HmRomAudio::OpllSampleRate);

		// 0 = YM2413
		OPLL_setChipType(_opll, 0);
		OPLL_resetPatch(_opll, 0);
	}

	~HmRomAudio()
	{
		OPLL_delete(_opll);
	}

	void Reset()
	{
		OPLL_reset(_opll);
	}

	void WriteOPLL(bool A0, uint8_t bus)
	{
		if(!A0) { // address
			_currentReg = bus;
		} else { // data
			OPLL_writeReg(_opll, _currentReg, bus);
		}
	}
};