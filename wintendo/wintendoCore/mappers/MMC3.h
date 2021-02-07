#pragma once
#pragma once

#include "common.h"
#include "NesSystem.h"

class MMC3 : public wtMapper
{
private:

	union BankSelect
	{
		struct BankSelectSemantic
		{
			uint8_t	bankReg			: 3;
			uint8_t	unused			: 3;
			uint8_t	prgRomBankMode	: 1;
			uint8_t	chrA12Inversion : 1;
		} sem;

		uint8_t byte;
	};

	BankSelect	bankSelect;
	uint8_t		R[8];
	uint8_t		prgRamBank[ KB_8 ];
	uint8_t		irqLatch;
	uint8_t		irqCounter;
	bool		irqEnable;
	int8_t		oldPrgBankMode;
	int8_t		oldChrBankMode;
	bool		bankDataInit;

	uint8_t		bank0;
	uint8_t		bank1;
	uint8_t		bank2;
	uint8_t		bank3;

	uint8_t GetMirrorMode()
	{
		return 0;
	}

	uint8_t MapMemory( const uint16_t address, const uint8_t regValue )
	{
		return 0;
	}

public:

	MMC3( const uint32_t _mapperId ) :
		irqLatch( 0x00 ),
		irqCounter( 0x00 ),
		irqEnable( false ),
		oldPrgBankMode( -1 ),
		oldChrBankMode( -1 ),
		bankDataInit( false ),
		bank0(0),
		bank1(0),
		bank2(0),
		bank3(0)
	{
		mapperId = _mapperId;
		bankSelect.byte = 0;
	}

	uint8_t OnLoadCpu() override
	{
		bank0 = 0,
		bank1 = 1,
		bank2 = ( 2 * system->cart.header.prgRomBanks ) - 2;
		bank3 = ( 2 * system->cart.header.prgRomBanks ) - 1;

		return 0;
	}

	uint8_t OnLoadPpu() override
	{
		return 0;
	}

	bool InWriteWindow( const uint16_t addr, const uint16_t offset ) override
	{
		const uint16_t address = ( addr + offset );
		return ( system->cart.GetMapperId() == mapperId ) && InRange( address, 0x6000, 0xFFFF );
	}

	void Clock() override
	{
		if( irqCounter <= 0 ) {		
			irqCounter = irqLatch;
		} else {
			--irqCounter;
		}

		if ( ( irqCounter == 0 ) && irqEnable ) {
			system->RequestIRQ();
		}
	}

	uint8_t	ReadRom( const uint16_t addr ) override
	{
		
		if ( InRange( addr, 0x8000, 0x9FFF ) )
		{
			const uint16_t bankAddr = ( addr - 0x8000 );
			return system->cart.GetPrgRomBank( bank0, KB_8 )[ bankAddr ];
		}
		else if ( InRange( addr, 0xA000, 0xBFFF ) )
		{
			const uint16_t bankAddr = ( addr - 0xA000 );
			return system->cart.GetPrgRomBank( bank1, KB_8 )[ bankAddr ];
		}
		else if ( InRange( addr, 0xC000, 0xDFFF ) )
		{
			const uint16_t bankAddr = ( addr - 0xC000 );
			return system->cart.GetPrgRomBank( bank2, KB_8 )[ bankAddr ];
		}
		else if ( InRange( addr, 0xE000, 0xFFFF ) )
		{
			const uint16_t bankAddr = ( addr - 0xE000 );
			return system->cart.GetPrgRomBank( bank3, KB_8 )[ bankAddr ];
		}
		else if ( InRange( addr, wtSystem::SramBase, wtSystem::SramEnd ) )
		{
			const uint16_t sramAddr = ( addr - wtSystem::SramBase );
			return prgRamBank[ sramAddr ];
		}		

		return 0;
	}

	uint8_t Write( const uint16_t addr, const uint16_t offset, const uint8_t value ) override
	{
		const uint16_t address = ( addr + offset );
		bool swapPrgBanks = false;
		bool swapChrBanks = false;

		if ( InRange( address, wtSystem::SramBase, wtSystem::SramEnd ) ) {
			const uint16_t sramAddr = ( address - wtSystem::SramBase );
			prgRamBank[ sramAddr ] = value;
			return 0;
		}

		if ( InRange( address, 0x8000, 0x9FFF ) )
		{
			if( ( address % 2 ) == 0 )
			{
				bankSelect.byte = value;

				if( bankSelect.sem.prgRomBankMode != oldPrgBankMode )
				{
					swapPrgBanks = bankDataInit;
					oldPrgBankMode = bankSelect.sem.prgRomBankMode;
				}

				if ( bankSelect.sem.chrA12Inversion != oldChrBankMode )
				{
					swapChrBanks = bankDataInit;
					oldChrBankMode = bankSelect.sem.chrA12Inversion;
				}
			}
			else
			{
				switch( bankSelect.sem.bankReg )
				{
					default: case 2: case 3: case 4: case 5:
						R[bankSelect.sem.bankReg] = value;
					break;

					case 0: case 1:
						R[bankSelect.sem.bankReg] = value & 0xFE;
					break;

					case 6: case 7:
						R[bankSelect.sem.bankReg] = value & 0x3F;
					break;
				}

				bankDataInit = true;
				swapPrgBanks = true;
				swapChrBanks = true;
			}
		}
		else if ( InRange( address, 0xA000, 0xBFFF ) )
		{
			if ( ( address % 2 ) == 0 )
			{
				if( !system->cart.header.controlBits0.fourScreenMirror )
				{
					system->mirrorMode = ( value & 0x01 ) ? MIRROR_MODE_HORIZONTAL : MIRROR_MODE_VERTICAL;
				}
			}
			else
			{
				// PRG RAM Protect
			}
		}
		else if ( InRange( address, 0xC000, 0xDFFF ) )
		{
			if ( ( address % 2 ) == 0 )	{
				irqLatch = value;
			} else {
				irqCounter = 0;
			}
		}
		else if ( InRange( address, 0xE000, 0xFFFF ) )
		{
			if ( ( address % 2 ) == 0 )
			{
				irqEnable = false;
			}
			else
			{
				irqEnable = true;
			}
		}

		if( swapPrgBanks )
		{
			const uint8_t lastBank = ( 2 * system->cart.header.prgRomBanks ) - 1;
			const uint8_t secondLastBank = ( 2 * system->cart.header.prgRomBanks ) - 2;

			if ( bankSelect.sem.prgRomBankMode )
			{
				bank0 = secondLastBank;
				bank1 = R[ 7 ];
				bank2 = R[ 6 ];
				bank3 = lastBank;
			}
			else
			{
				bank0 = R[ 6 ];
				bank1 = R[ 7 ];
				bank2 = secondLastBank;
				bank3 = lastBank;
			}

		}

		if( swapChrBanks )
		{
			const uint32_t chrRomStart = system->cart.header.prgRomBanks * KB_16;
			if ( bankSelect.sem.chrA12Inversion )
			{
				memcpy( &system->ppu.vram[0x0000], &system->cart.rom[chrRomStart + R[2] * KB_1], KB_1 );
				memcpy( &system->ppu.vram[0x0400], &system->cart.rom[chrRomStart + R[3] * KB_1], KB_1 );
				memcpy( &system->ppu.vram[0x0800], &system->cart.rom[chrRomStart + R[4] * KB_1], KB_1 );
				memcpy( &system->ppu.vram[0x0C00], &system->cart.rom[chrRomStart + R[5] * KB_1], KB_1 );
				memcpy( &system->ppu.vram[0x1000], &system->cart.rom[chrRomStart + R[0] * KB_1], KB_2 );
				memcpy( &system->ppu.vram[0x1800], &system->cart.rom[chrRomStart + R[1] * KB_1], KB_2 );
			}
			else
			{
				memcpy( &system->ppu.vram[0x0000], &system->cart.rom[chrRomStart + R[0] * KB_1], KB_2 );
				memcpy( &system->ppu.vram[0x0800], &system->cart.rom[chrRomStart + R[1] * KB_1], KB_2 );
				memcpy( &system->ppu.vram[0x1000], &system->cart.rom[chrRomStart + R[2] * KB_1], KB_1 );
				memcpy( &system->ppu.vram[0x1400], &system->cart.rom[chrRomStart + R[3] * KB_1], KB_1 );
				memcpy( &system->ppu.vram[0x1800], &system->cart.rom[chrRomStart + R[4] * KB_1], KB_1 );
				memcpy( &system->ppu.vram[0x1C00], &system->cart.rom[chrRomStart + R[5] * KB_1], KB_1 );
			}
		}
		
		return 0;
	}

	void Serialize( Serializer& serializer, const serializeMode_t mode ) override
	{
		serializer.Next8b( irqLatch, mode );
		serializer.Next8b( irqCounter, mode );
		serializer.Next8b( bankSelect.byte, mode );
		serializer.Next8b( bank0, mode );
		serializer.Next8b( bank1, mode );
		serializer.Next8b( bank2, mode );
		serializer.Next8b( bank3, mode );
		serializer.NextBool( irqEnable, mode );
		serializer.NextBool( bankDataInit, mode );
		serializer.NextChar( oldPrgBankMode, mode );
		serializer.NextChar( oldChrBankMode, mode );
		serializer.NextArray( reinterpret_cast<uint8_t*>( &R[ 0 ] ), 8 * sizeof( R[ 0 ] ), mode );
		serializer.NextArray( reinterpret_cast<uint8_t*>( &prgRamBank[ 0 ] ), KB_8, mode );
	}
};