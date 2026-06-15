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

	// ATtiny85 USI
	bool _i2cSCL = 1;
	bool _i2cSDA = 1;
	bool _i2cSCLprev = 1;
	bool _i2cSDAprev = 1;

	uint8_t _usiDR = 0; // shift register
	uint8_t _usiCounter = 0; // clock edge counter
	bool _isTransferActive = false;

	// ATtiny85 ADPCM Player
	bool _85A0 = 0;
	uint8_t _aram[65536] = {};

	uint8_t _85reg = 0;

	bool _A85ADDRsecondWrite = false;

	static constexpr int _deltaTbl[] = {
		-2, -1, 1, 2, 
		-4, -1, 1, 4, 
		-8, -2, 2, 8, 
		-12, -3, 3, 12, 
		-20, -5, 5, 20, 
		-32, -8, 8, 32, 
		-48, -12, 12, 48, 
		-80, -20, 20, 80
	};

	uint16_t _dataPtr = 0;
	bool _isPCMplaying = false;
	uint16_t _PCMtimer = 0;
	uint8_t _PCMOutput = 128;
	uint8_t _delatTblIndex = 0;
	uint8_t _sampleIndex = 0; // in block

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
			int16_t output = OPLL_calc(_opll) + (_PCMOutput - 128) * 12;
			_console->GetApu()->AddExpansionAudioDelta(AudioChannel::VRC7, output - _previousOutput);
			_previousOutput = output;
			_clockTimer = ((double)_console->GetMasterClockRate()) / HmRomAudio::OpllSampleRate;
		}

		// Process ATtiny85
		if(_i2cSCLprev && _i2cSCL && _i2cSDAprev && !_i2cSDA) { // I2C START
			_usiCounter = 0;
			_usiDR = 0;
			_isTransferActive = true;

		} else if(_i2cSCLprev && _i2cSCL && !_i2cSDAprev && _i2cSDA) { // I2C STOP
			_isTransferActive = false;

		} else if(_isTransferActive && (_i2cSCLprev != _i2cSCL)) {
			_usiCounter++;

			if(!_i2cSCLprev && _i2cSCL) {
				_usiDR = (_usiDR << 1) | (_i2cSDA ? 1 : 0);
			}

			if(_usiCounter >= 16) {
				_usiCounter = 0;
				_isTransferActive = false;

				// USI Overflow Interrupt (essentially ATtiny85 write event)
				if(_85A0) { // ATtiny85 addr
					_85reg = _usiDR;
				} else { // ATtiny85 data
					switch(_85reg) {
						case 0: // A85ADDR
							if(!_A85ADDRsecondWrite) { // first write
								_dataPtr = (_dataPtr & 0x00FF) | _usiDR << 8; // MSB first (just like PPUADDR)
							} else {
								_dataPtr = (_dataPtr & 0xFF00) | _usiDR; // LSB later (just like PPUADDR)
							}
							_A85ADDRsecondWrite = !_A85ADDRsecondWrite;
							break;
						case 1: // A85DATA
							_aram[_dataPtr++] = _usiDR;
							break;
						case 2: // A85HALT
							if(_usiDR == 0) {
								_PCMOutput = 128; // reset delta output if sample began playing
								_sampleIndex = 0;
								_isPCMplaying = true;
							} else {
								_A85ADDRsecondWrite = false; // reset address toggle
								_isPCMplaying = false;
							}
							break;
					}
				}
			}
		}

		_i2cSCLprev = _i2cSCL;
		_i2cSDAprev = _i2cSDA;

		// PCM handlin
		if(_isPCMplaying) {
			_PCMtimer--;
			if(_PCMtimer == 0) { // PCM clock
				_PCMtimer = (double)_console->GetMasterClockRate() / 16000; // reload timer

				if(_sampleIndex == 0) { // header byte
					if((_aram[_dataPtr] & 0x10) == 0x10) { // if sample end
						_isPCMplaying = false;
						return;
					}
					_delatTblIndex = (_aram[_dataPtr] >> 5) & 0x07;
					_sampleIndex = 2;
				}
				_PCMOutput += _deltaTbl[
					(
						(
							_aram[_dataPtr + _sampleIndex / 4] >> (((_sampleIndex ^ 0x03) & 0x03) << 1)
						) & 0x03
					) | (_delatTblIndex << 2)
				];
				_sampleIndex++;
				if(_sampleIndex == 32) { // sample index out of range
					_dataPtr += 8;
					_sampleIndex = 0;
				}
			}
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
		if(A0 == 0) { // address
			_currentReg = bus;
		} else { // data
			OPLL_writeReg(_opll, _currentReg, bus);
		}
	}

	void WriteATtiny85(uint8_t data)
	{
		/*
				7  bit  0
				---- ----
				CAxx xxxD
				||      |
				||      +- ATtiny85 SDA
				|+-------- Address / Data
				+--------- ATtiny85 SCL
		*/
		_i2cSCL = data & 0x80;
		_i2cSDA = data & 0x01;
		_85A0 = (data & 0x40) == 0;
	}

	uint8_t getOPLLcurrentReg()
	{
		return _currentReg;
	}

	bool getA85SCL()
	{
		return _i2cSCL;
	}

	bool getA85SDA()
	{
		return _i2cSDA;
	}

	bool getA85A0()
	{
		return !_85A0;
	}

	uint16_t getA85ptr()
	{
		return _dataPtr;
	}

	uint8_t getA85DAC()
	{
		return _PCMOutput;
	}

	uint8_t getA85Reg()
	{
		return _85reg;
	}

	bool getPCMHalt()
	{
		return !_isPCMplaying;
	}

	uint8_t getCurrentUSIDR()
	{
		return _usiDR;
	}
	
	uint8_t getUSICounter()
	{
		return _usiCounter;
	}
};