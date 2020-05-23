#include "stdafx.h"
#include <iostream>
#include <iomanip>
#include <Windows.h>
#include <chrono>
#include <thread>
#include <fstream>
#include <string>
#include <sstream>
#include <assert.h>
#include <map>
#include <bitset>
#include "common.h"
#include "debug.h"
#include "mos6502.h"
#include "NesSystem.h"

using namespace std;


struct RegDebugFile
{
	uint8_t X;
	uint8_t Y;
	uint8_t A;
	uint8_t SP;
	ProcessorStatus P;
	uint16_t PC;
	int32_t curScanline;
	cpuCycle_t cpuCycles;
	ppuCycle_t ppuCycles;
	cpuCycle_t instrCycles;
};


void DebugPrintRegisters( const RegDebugFile& debugFile, string& regStr, bool print )
{
#if DEBUG_ADDR == 1
	if ( !print )
		return;

	stringstream sStream;

	sStream << uppercase << "A:" << setfill( '0' ) << setw( 2 ) << hex << static_cast<int>( debugFile.A ) << setw( 1 ) << " ";
	sStream << uppercase << "X:" << setfill( '0' ) << setw( 2 ) << hex << static_cast<int>( debugFile.X ) << setw( 1 ) << " ";
	sStream << uppercase << "Y:" << setfill( '0' ) << setw( 2 ) << hex << static_cast<int>( debugFile.Y ) << setw( 1 ) << " ";
	sStream << uppercase << "P:" << setfill( '0' ) << setw( 2 ) << hex << static_cast<int>( debugFile.P.byte ) << setw( 1 ) << " ";
	sStream << uppercase << "SP:" << setfill( '0' ) << setw( 2 ) << hex << static_cast<int>( debugFile.SP ) << setw( 1 ) << " ";
	sStream << uppercase << "PPU:" << setfill( ' ' ) << setw( 3 ) << dec << debugFile.ppuCycles.count() << "," << setw( 3 ) << ( debugFile.curScanline + 1 ) << " ";
	sStream << uppercase << "CYC:" << dec << ( 7 + debugFile.cpuCycles.count() ) << "\0"; // 7 is to match the init value from the nintendolator log

	regStr = sStream.str();
#endif // #if DEBUG_ADDR == 1
}


void DebugPrintFlushInstruction( NesSystem& system, const uint16_t instrBegin, const uint8_t byteCode, uint8_t operands, char* mnemonic, const string& regStr )
{
#if DEBUG_ADDR == 1
	if ( system.cpu.logFrameCount > 0 )
	{
		int disassemblyBytes[6] = { byteCode, system.GetMemory( instrBegin + 1 ), system.GetMemory( instrBegin + 2 ),'\0' };
		stringstream hexString;
		stringstream logLine;

		if ( operands == 1 )
		{
			hexString << uppercase << setfill( '0' ) << setw( 2 ) << hex << disassemblyBytes[0] << " " << setw( 2 ) << disassemblyBytes[1]; \
		}
		else if ( operands == 2 )
		{
			hexString << uppercase << setfill( '0' ) << setw( 2 ) << hex << disassemblyBytes[0] << " " << setw( 2 ) << disassemblyBytes[1] << " " << setw( 2 ) << disassemblyBytes[2]; \
		}
		else
		{
			hexString << uppercase << setfill( '0' ) << setw( 2 ) << hex << disassemblyBytes[0];
		}
		
		logLine << uppercase << setfill( '0' ) << setw( 4 ) << hex << instrBegin << setfill( ' ' ) << "  " << setw( 10 ) << left << hexString.str() << mnemonic << " " << setw( 28 ) << left << system.cpu.debugAddr.str() << right << regStr;

		system.cpu.logFile << logLine.str() << endl;
	}
#endif
}


template <class AddrFunctor>
uint8_t Cpu6502::Read()
{
	CpuAddrInfo addrInfo;
	AddrFunctor(*this)( addrInfo );

	return addrInfo.value;
}


template <class AddrFunctor>
void Cpu6502::Write( const uint8_t value )
{
	//	assert( params.getAddr != &Cpu6502::Immediate );

	CpuAddrInfo addrInfo;
	AddrFunctor( *this )( addrInfo );

	if ( NesSystem::IsPpuRegister( addrInfo.addr ) )
	{
		system->ppu.Reg( addrInfo.addr, value );
	}
	else if ( NesSystem::IsDMA( addrInfo.addr) )
	{
		system->ppu.OAMDMA( value );
	}
	else if( NesSystem::IsInputRegister( addrInfo.addr) )
	{
		const bool prevStrobeOn = system->strobeOn;
		system->strobeOn = ( value & 0x01 );

		if ( prevStrobeOn && !system->strobeOn )
		{
			system->btnShift[0] = 0;
			system->btnShift[1] = 0;
		//	system->controllerBuffer0 = GetKeyBuffer() & ( (ButtonFlags)0X80 );
		}
	}
	else if ( addrInfo.isAccumulator )
	{
		A = value;
	}
	else
	{
		// TODO: Replace stub logic
		bool isUnrom = system->cart->header.controlBits0.mapperNumberLower == 2;
		uint32_t bank = ( value & 0x07 );
		if ( isUnrom && ( addrInfo.addr >= 0x8000 ) && ( addrInfo.addr <= 0xFFFF ) && bank != system->prgRomBank )
		{
			//assert( value >= 1 );
			// The bits 0-2 of the byte *written* at $8000-$FFFF not the address
			memcpy( system->memory + NesSystem::Bank0, system->cart->rom + bank * NesSystem::BankSize, NesSystem::BankSize );
			system->prgRomBank = bank;
		//	assert(0);
		}
		else
		{
			uint32_t address = ( addrInfo.addr + addrInfo.offset ) % NesSystem::MemoryWrap;
			system->WritePhysicalMemory( address, value );
		}
	}
} 


OP_DEF( SEC )
{
	P.bit.c = 1;

	return 0;
}


OP_DEF( SEI )
{
	P.bit.i = 1;

	return 0;
}


OP_DEF( SED )
{
	P.bit.d = 1;

	return 0;
}


OP_DEF( CLC )
{
	P.bit.c = 0;

	return 0;
}


OP_DEF( CLI )
{
	P.bit.i = 0;

	return 0;
}


OP_DEF( CLV )
{
	P.bit.v = 0;

	return 0;
}


OP_DEF( CLD )
{
	P.bit.d = 0;

	return 0;
}


OP_DEF( CMP )
{
	const uint16_t result = ( A - Read<AddrMode>() );

	P.bit.c = !CheckCarry( result );
	SetAluFlags( result );

	return 0;
}


OP_DEF( CPX )
{
	const uint16_t result = ( X - Read<AddrMode>() );

	P.bit.c = !CheckCarry( result );
	SetAluFlags( result );

	return 0;
}


OP_DEF( CPY )
{
	const uint16_t result = ( Y - Read<AddrMode>() );

	P.bit.c = !CheckCarry( result );
	SetAluFlags( result );

	return 0;
}


OP_DEF( LDA )
{
	A = Read<AddrMode>();

	SetAluFlags( A );

	return 0;
}


OP_DEF( LDX )
{
	X = Read<AddrMode>();

	SetAluFlags( X );

	return 0;
}


OP_DEF( LDY )
{
	Y = Read<AddrMode>();

	SetAluFlags( Y );

	return 0;
}


OP_DEF( STA )
{
	Write<AddrMode>( A );

	return 0;
}


OP_DEF( STX )
{
	Write<AddrMode>( X );

	return 0;
}


OP_DEF( STY )
{
	Write<AddrMode>( Y );

	return 0;
}


OP_DEF( TXS )
{
	SP = X;

	return 0;
}


OP_DEF( TXA )
{
	A = X;
	SetAluFlags( A );

	return 0;
}


OP_DEF( TYA )
{
	A = Y;
	SetAluFlags( A );

	return 0;
}


OP_DEF( TAX )
{
	X = A;
	SetAluFlags( X );

	return 0;
}


OP_DEF( TAY )
{
	Y = A;
	SetAluFlags( Y );

	return 0;
}


OP_DEF( TSX )
{
	X = SP;
	SetAluFlags( X );

	return 0;
}


OP_DEF( ADC )
{
	// http://nesdev.com/6502.txt, "INSTRUCTION OPERATION - ADC"
	const uint8_t M = Read<AddrMode>();
	const uint16_t src = A;
	const uint16_t carry = ( P.bit.c ) ? 1 : 0;
	const uint16_t temp = A + M + carry;

	A = ( temp & 0xFF );

	P.bit.z = CheckZero( temp );
	P.bit.v = CheckOverflow( M, temp, A );
	SetAluFlags( A );

	P.bit.c = ( temp > 0xFF );

	return 0;
}


OP_DEF( SBC )
{
	uint8_t M = Read<AddrMode>();
	const uint16_t carry = ( P.bit.c ) ? 0 : 1;
	const uint16_t result = A - M - carry;

	SetAluFlags( result );

	P.bit.v = ( CheckSign( A ^ result ) && CheckSign( A ^ M ) );
	P.bit.c = !CheckCarry( result );

	A = result & 0xFF;

	return 0;
}


OP_DEF( INX )
{
	++X;
	SetAluFlags( X );

	return 0;
}


OP_DEF( INY )
{
	++Y;
	SetAluFlags( Y );

	return 0;
}


OP_DEF( DEX )
{
	--X;
	SetAluFlags( X );

	return 0;
}


OP_DEF( DEY )
{
	--Y;
	SetAluFlags( Y );

	return 0;
}


OP_DEF( INC )
{
	const uint8_t result = Read<AddrMode>() + 1;

	Write<AddrMode>( result );

	SetAluFlags( result );

	return 0;
}


OP_DEF( DEC )
{
	const uint8_t result = Read<AddrMode>() - 1;

	Write<AddrMode>( result );

	SetAluFlags( result );

	return 0;
}


OP_DEF( PHP )
{
	Push( P.byte | STATUS_UNUSED | STATUS_BREAK );

	return 0;
}


OP_DEF( PHA )
{
	Push( A );

	return 0;
}


OP_DEF( PLA )
{
	A = Pull();

	SetAluFlags( A );

	return 0;
}


OP_DEF( PLP )
{
	// https://wiki.nesdev.com/w/index.php/Status_flags
	const uint8_t status = ~STATUS_BREAK & Pull();
	P.byte = status | ( P.byte & STATUS_BREAK ) | STATUS_UNUSED;

	return 0;
}


OP_DEF( NOP )
{
	return 0;
}


OP_DEF( ASL )
{
	uint8_t M = Read<AddrMode>();

	P.bit.c = !!( M & 0x80 );
	M <<= 1;
	Write<AddrMode>( M );
	SetAluFlags( M );

	return 0;
}


OP_DEF( LSR )
{
	uint8_t M = Read<AddrMode>();

	P.bit.c = ( M & 0x01 );
	M >>= 1;
	Write<AddrMode>( M );
	SetAluFlags( M );

	return 0;
}


OP_DEF( AND )
{
	A &= Read<AddrMode>();

	SetAluFlags( A );

	return 0;
}


OP_DEF( BIT )
{
	const uint8_t M = Read<AddrMode>();

	P.bit.z = !( A & M );
	P.bit.n = CheckSign( M );
	P.bit.v = !!( M & 0x40 );

	return 0;
}


OP_DEF( EOR )
{
	A ^= Read<AddrMode>();

	SetAluFlags( A );

	return 0;
}


OP_DEF( ORA )
{
	A |= Read<AddrMode>();

	SetAluFlags( A );

	return 0;
}


OP_DEF( JMP )
{
	PC = ReadAddressOperand();

	DEBUG_ADDR_JMP

	return 0;
}


OP_DEF( JMPI )
{
	const uint16_t addr0 = ReadAddressOperand();

	// Hardware bug - http://wiki.nesdev.com/w/index.php/Errata
	if ( ( addr0 & 0xff ) == 0xff )
	{
		const uint16_t addr1 = Combine( 0x00, ReadOperand( 1 ) );

		PC = ( Combine( system->GetMemory( addr0 ), system->GetMemory( addr1 ) ) );
	}
	else
	{
		PC = ( Combine( system->GetMemory( addr0 ), system->GetMemory( addr0 + 1 ) ) );
	}

	DEBUG_ADDR_JMPI

	return 0;
}


OP_DEF( JSR )
{
	uint16_t retAddr = PC + 1;

	Push( ( retAddr >> 8 ) & 0xFF );
	Push( retAddr & 0xFF );

	PC = ReadAddressOperand();

	DEBUG_ADDR_JSR

	return 0;
}


OP_DEF( BRK )
{
	P.bit.b = 1;

	interruptTriggered = true;

	return 0;
}


OP_DEF( RTS )
{
	const uint8_t loByte = Pull();
	const uint8_t hiByte = Pull();

	PC = 1 + Combine( loByte, hiByte );

	return 0;
}


OP_DEF( RTI )
{
	PLP<AddrMode>();

	const uint8_t loByte = Pull();
	const uint8_t hiByte = Pull();

	PC = Combine( loByte, hiByte );

	return 0;
}


OP_DEF( BMI )
{
	return Branch( ( P.bit.n ) );
}


OP_DEF( BVS )
{
	return Branch( ( P.bit.v ) );
}


OP_DEF( BCS )
{
	return Branch( ( P.bit.c ) );
}


OP_DEF( BEQ )
{
	return Branch( ( P.bit.z ) );
}


OP_DEF( BPL )
{
	return Branch( !( P.bit.n ) );
}


OP_DEF( BVC )
{
	return Branch( !( P.bit.v ) );
}


OP_DEF( BCC )
{
	return Branch( !( P.bit.c ) );
}


OP_DEF( BNE )
{
	return Branch( !( P.bit.z ) );
}


OP_DEF( ROL )
{
	uint16_t temp = Read<AddrMode>() << 1;
	temp = ( P.bit.c ) ? temp | 0x0001 : temp;

	P.bit.c = CheckCarry( temp );

	temp &= 0xFF;

	SetAluFlags( temp );

	Write<AddrMode>( temp & 0xFF );

	return 0;
}


OP_DEF( ROR )
{
	uint16_t temp = ( P.bit.c ) ? Read<AddrMode>() | 0x0100 : Read<AddrMode>();

	P.bit.c = ( temp & 0x01 );

	temp >>= 1;

	SetAluFlags( temp );

	Write<AddrMode>( temp & 0xFF );

	return 0;
}


OP_DEF( Illegal )
{
	assert( 0 );

	return 0;
}


OP_DEF( SKB )
{
	PC += 1;

	return 0;
}


OP_DEF( SKW )
{
	PC += 2;

	return 0;
}


ADDR_MODE_DEF( None )
{
	addrInfo = CpuAddrInfo{ Cpu6502::InvalidAddress, 0, 0, false, false };
}


ADDR_MODE_DEF( IndexedIndirect )
{
	const uint8_t targetAddress = ( cpu.ReadOperand(0) + cpu.X );
	uint32_t address = cpu.CombineIndirect( targetAddress, 0x00, NesSystem::ZeroPageWrap );

	uint8_t value = cpu.system->GetMemory( address );
	addrInfo = CpuAddrInfo{ address, 0, value, false, false };

	DEBUG_ADDR_INDEXED_INDIRECT
}


ADDR_MODE_DEF( IndirectIndexed )
{
	uint32_t address = cpu.CombineIndirect( cpu.ReadOperand(0), 0x00, NesSystem::ZeroPageWrap );

	const uint16_t offset = ( address + cpu.Y ) % NesSystem::MemoryWrap;

	uint8_t value = cpu.system->GetMemory( offset );

	cpu.instructionCycles += cpuCycle_t( cpu.AddressCrossesPage( address, cpu.Y ) ); // TODO: make consistent

	addrInfo = CpuAddrInfo{ address, cpu.Y, value, false, false };

	DEBUG_ADDR_INDIRECT_INDEXED
}


ADDR_MODE_DEF( Absolute )
{
	uint32_t address = cpu.ReadAddressOperand();

	uint8_t value = cpu.system->GetMemory( address );

	addrInfo = CpuAddrInfo{ address, 0, value, false, false };

	DEBUG_ADDR_ABS
}


ADDR_MODE_DEF( IndexedAbsoluteX )
{
	cpu.IndexedAbsolute( cpu.X, addrInfo );
}


ADDR_MODE_DEF( IndexedAbsoluteY )
{
	cpu.IndexedAbsolute( cpu.Y, addrInfo );
}


ADDR_MODE_DEF( Zero )
{
	const uint16_t targetAddresss = Combine( cpu.ReadOperand(0), 0x00 );

	uint32_t address = ( targetAddresss % NesSystem::ZeroPageWrap );

	uint8_t value = cpu.system->GetMemory( address );

	addrInfo = CpuAddrInfo{ address, 0, value, false, false };

	DEBUG_ADDR_ZERO
}


ADDR_MODE_DEF( IndexedZeroX )
{
	cpu.IndexedZero( cpu.X, addrInfo );
}


ADDR_MODE_DEF( IndexedZeroY )
{
	cpu.IndexedZero( cpu.Y, addrInfo );
}


ADDR_MODE_DEF( Immediate )
{
	uint8_t value = cpu.ReadOperand(0);

	uint32_t address = Cpu6502::InvalidAddress;

	addrInfo = CpuAddrInfo{ address, 0, value, false, true };

	DEBUG_ADDR_IMMEDIATE
}


ADDR_MODE_DEF( Accumulator )
{
	addrInfo = CpuAddrInfo{ Cpu6502::InvalidAddress, 0, cpu.A, true, false };

	DEBUG_ADDR_ACCUMULATOR
}


void Cpu6502::Push( const uint8_t value )
{
	system->GetStack() = value;
	SP--;
}


void Cpu6502::PushByte( const uint16_t value )
{
	Push( ( value >> 8 ) & 0xFF );
	Push( value & 0xFF );
}


uint8_t Cpu6502::Pull()
{
	SP++;
	return system->GetStack();
}


uint16_t Cpu6502::PullWord()
{
	const uint8_t loByte = Pull();
	const uint8_t hiByte = Pull();

	return Combine( loByte, hiByte );
}


uint8_t Cpu6502::Branch( const bool takeBranch )
{
	const uint16_t offset		= static_cast< int8_t >( ReadOperand(0) );
	const uint16_t branchedPC	= PC + offset + 1; // used in debug print, clean-up

	uint8_t cycles = 0;

	if ( takeBranch )
	{
		cycles = 1 + AddressCrossesPage( PC + 1, offset );

		PC = branchedPC;
	}
	else
	{
		PC += 1;
	}

	DEBUG_ADDR_BRANCH

	instructionCycles += cpuCycle_t(cycles);
	return 0;
}


void Cpu6502::SetAluFlags( const uint16_t value )
{
	P.bit.z = CheckZero( value );
	P.bit.n = CheckSign( value );
}


bool Cpu6502::CheckSign( const uint16_t checkValue )
{
	return ( checkValue & 0x0080 );
}


bool Cpu6502::CheckCarry( const uint16_t checkValue )
{
	return ( checkValue > 0x00ff );
}


bool Cpu6502::CheckZero( const uint16_t checkValue )
{
	return ( checkValue == 0 );
}


bool Cpu6502::CheckOverflow( const uint16_t src, const uint16_t temp, const uint8_t finalValue )
{
	const uint8_t signedBound = 0x80;
	return CheckSign( finalValue ^ src ) && CheckSign( temp ^ src ) && !CheckCarry( temp );
}


uint8_t Cpu6502::AddressCrossesPage( const uint16_t address, const uint16_t offset )
{
	if( opCode == 0x9D || opCode == 0x99 ) // FIXME: massive hack
	{
		return 0;
	}

	const uint32_t targetAddress = ( address + offset ) % NesSystem::MemoryWrap;

	return ( ( targetAddress & 0xFF00 ) == ( address & 0xFF00 ) ) ? 0 : 1;
}


uint16_t Cpu6502::CombineIndirect( const uint8_t lsb, const uint8_t msb, const uint32_t wrap )
{
	const uint16_t address = Combine( lsb, msb );
	const uint8_t loByte = system->GetMemory( address % wrap );
	const uint8_t hiByte = system->GetMemory( ( address + 1 ) % wrap );
	const uint16_t value = Combine( loByte, hiByte );

	return value;
}


void Cpu6502::DebugPrintIndexZero( const uint8_t& reg, const uint32_t address, const uint32_t targetAddresss )
{
#if DEBUG_ADDR == 1
	uint8_t& value = system->GetMemory( address );
	debugAddr.str( std::string() );
	debugAddr << uppercase << "$" << setw( 2 ) << hex << targetAddresss << ",";
	debugAddr << ( ( &reg == &X ) ? "X" : "Y" );
	debugAddr << setfill( '0' ) << " @ " << setw( 2 ) << hex << address;
	debugAddr << " = " << setw( 2 ) << hex << static_cast< uint32_t >( value );
#endif // #if DEBUG_ADDR == 1
}


void Cpu6502::IndexedZero( const uint8_t& reg, CpuAddrInfo& addrInfo )
{
	const uint16_t targetAddresss = Combine( ReadOperand(0), 0x00 );

	uint32_t address = ( targetAddresss + reg ) % NesSystem::ZeroPageWrap;

	uint8_t value = system->GetMemory( address );

	addrInfo = CpuAddrInfo{ address, 0, value, false, false };

	DebugPrintIndexZero( reg, address, targetAddresss );
}


void Cpu6502::IndexedAbsolute( const uint8_t& reg, CpuAddrInfo& addrInfo )
{
	const uint16_t targetAddresss = ReadAddressOperand();

	uint32_t address = ( targetAddresss + reg ) % NesSystem::MemoryWrap;

	uint8_t value = system->GetMemory( address );

	instructionCycles += cpuCycle_t( AddressCrossesPage( targetAddresss, reg ) ); // TODO: make consistent

	addrInfo = CpuAddrInfo{ address, 0, value, false, false };

	DEBUG_ADDR_INDEXED_ABS
}


uint8_t Cpu6502::NMI()
{
	PushByte( PC - 1 );
	// http://wiki.nesdev.com/w/index.php/CPU_status_flag_behavior
	Push( P.byte | STATUS_BREAK );

	P.bit.i = 1;

	PC = nmiVector;

	return 7;
}


uint8_t Cpu6502::IRQ()
{
	return NMI();
}


void Cpu6502::AdvanceProgram( const uint16_t places )
{
	PC += places;
}


uint8_t& Cpu6502::ReadOperand( const uint16_t offset ) const
{
	return system->GetMemory( PC + offset );
}


uint16_t Cpu6502::ReadAddressOperand() const
{
	const uint8_t loByte = ReadOperand(0);
	const uint8_t hiByte = ReadOperand(1);

	return Combine( loByte, hiByte );
}


cpuCycle_t Cpu6502::Exec()
{
	instructionCycles = cpuCycle_t( 0 );

	if ( oamInProcess )
	{
		// http://wiki.nesdev.com/w/index.php/PPU_registers#OAMDMA
		if ( ( cycle % 2 ) == cpuCycle_t( 0 ) )
			instructionCycles += cpuCycle_t( 514 );
		else
			instructionCycles += cpuCycle_t( 513 );

		oamInProcess = false;

		return instructionCycles;
	}

	const uint16_t instrBegin = PC;

	uint8_t curbyte = system->GetMemory( instrBegin );

	AdvanceProgram(1);

	if ( resetTriggered )
	{
	}

	if ( interruptTriggered )
	{
		NMI();

		interruptTriggered = false;

		//Exec();

		return cpuCycle_t( 0 );
	}

	instructionCycles += LookupFunction( instrBegin, curbyte );

	return instructionCycles;
}


bool Cpu6502::Step( const cpuCycle_t& nextCycle )
{
	while ( ( cycle < nextCycle ) && !forceStop )
	{
		cycle += cpuCycle_t( Exec() );
	}

	return !forceStop;
}


cpuCycle_t Cpu6502::LookupFunction( const uint16_t instrBegin, const uint8_t byteCode )
{
#if DEBUG_ADDR == 1
	debugAddr.str( std::string() );
	string regStr;

	RegDebugFile debugReg = { X, Y, A, SP, P, PC, system->ppu.currentScanline, cycle, system->ppu.scanelineCycle };
#endif // #if DEBUG_ADDR == 1

	// TODO: Bundle into instruction data
	cpuCycle_t retVal = cpuCycle_t( 0 );
	uint8_t operands = 0;
	char* mnemonic = "";
	opCode = 0;

	switch ( byteCode )
	{
		OP( 0x00, BRK, 0, 7 )
		OP_ADDR( 0x01, ORA, IndexedIndirect, 1, 6 )
		OP_ILLEGAL( 0x02 )
		OP_ILLEGAL( 0x03 )
		OP( 0x04, SKB, 0, 3 )
		OP_ADDR( 0x05, ORA, Zero, 1, 3 )
		OP_ADDR( 0x06, ASL, Zero, 1, 5 )
		OP_ILLEGAL( 0x07 )
		OP( 0x08, PHP, 0, 3 )
		OP_ADDR( 0x09, ORA, Immediate, 1, 2 )
		OP_ADDR( 0x0A, ASL, Accumulator, 0, 2 )
		OP_ILLEGAL( 0x0B )
		OP( 0x0C, SKW, 0, 4 )
		OP_ADDR( 0x0D, ORA, Absolute, 2, 4 )
		OP_ADDR( 0x0E, ASL, Absolute, 2, 6 )
		OP_ILLEGAL( 0x0F )
		OP_JMP( 0x10, BPL, 1, 2 )
		OP_ADDR( 0x11, ORA, IndirectIndexed, 1, 5 )
		OP_ILLEGAL( 0x12 )
		OP_ILLEGAL( 0x13 )
		OP( 0x14, SKB, 0, 4 )
		OP_ADDR( 0x15, ORA, IndexedZeroX, 1, 4 )
		OP_ADDR( 0x16, ASL, IndexedZeroX, 1, 6 )
		OP_ILLEGAL( 0x17 )
		OP( 0x18, CLC, 0, 2 )
		OP_ADDR( 0x19, ORA, IndexedAbsoluteY, 2, 4 )
		OP( 0x1A, NOP, 0, 2 )
		OP_ILLEGAL( 0x1B )
		OP( 0x1C, SKW, 0, 4 )
		OP_ADDR( 0x1D, ORA, IndexedAbsoluteX, 2, 4 )
		OP_ADDR( 0x1E, ASL, IndexedAbsoluteX, 2, 7 )
		OP_ILLEGAL( 0x1F )
		OP_JMP( 0x20, JSR, 2, 6 )
		OP_ADDR( 0x21, AND, IndexedIndirect, 1, 6 )
		OP_ILLEGAL( 0x22 )
		OP_ILLEGAL( 0x23 )
		OP_ADDR( 0x24, BIT, Zero, 1, 3 )
		OP_ADDR( 0x25, AND, Zero, 1, 3 )
		OP_ADDR( 0x26, ROL, Zero, 1, 5 )
		OP_ILLEGAL( 0x27 )
		OP( 0x28, PLP, 0, 4 )
		OP_ADDR( 0x29, AND, Immediate, 1, 2 )
		OP_ADDR( 0x2A, ROL, Accumulator, 0, 2 )
		OP_ILLEGAL( 0x2B )
		OP_ADDR( 0x2C, BIT, Absolute, 2, 4 )
		OP_ADDR( 0x2D, AND, Absolute, 2, 4 )
		OP_ADDR( 0x2E, ROL, Absolute, 2, 6 )
		OP_ILLEGAL( 0x2F )
		OP_JMP( 0x30, BMI, 1, 2 )
		OP_ADDR( 0x31, AND, IndirectIndexed, 1, 5 )
		OP_ILLEGAL( 0x32 )
		OP_ILLEGAL( 0x33 )
		OP( 0x34, SKB, 0, 4 )
		OP_ADDR( 0x35, AND, IndexedZeroX, 1, 4 )
		OP_ADDR( 0x36, ROL, IndexedZeroX, 1, 6 )
		OP_ILLEGAL( 0x37 )
		OP( 0x38, SEC, 0, 2 )
		OP_ADDR( 0x39, AND, IndexedAbsoluteY, 2, 4 )
		OP( 0x3A, NOP, 0, 2 )
		OP_ILLEGAL( 0x3B )
		OP( 0x3C, SKW, 0, 4 )
		OP_ADDR( 0x3D, AND, IndexedAbsoluteX, 2, 4 )
		OP_ADDR( 0x3E, ROL, IndexedAbsoluteX, 2, 7 )
		OP_ILLEGAL( 0x3F )
		OP_JMP( 0x40, RTI, 0, 6 )
		OP_ADDR( 0x41, EOR, IndexedIndirect, 1, 6 )
		OP_ILLEGAL( 0x42 )
		OP_ILLEGAL( 0x43 )
		OP( 0x44, SKB, 0, 3 )
		OP_ADDR( 0x45, EOR, Zero, 1, 3 )
		OP_ADDR( 0x46, LSR, Zero, 1, 5 )
		OP_ILLEGAL( 0x47 )
		OP( 0x48, PHA, 0, 3 )
		OP_ADDR( 0x49, EOR, Immediate, 1, 2 )
		OP_ADDR( 0x4A, LSR, Accumulator, 0, 2 )
		OP_ILLEGAL( 0x4B )
		OP_JMP( 0x4C, JMP, 2, 3 )
		OP_ADDR( 0x4D, EOR, Absolute, 2, 4 )
		OP_ADDR( 0x4E, LSR, Absolute, 2, 6 )
		OP_ILLEGAL( 0x4F )
		OP_JMP( 0x50, BVC, 1, 2 )
		OP_ADDR( 0x51, EOR, IndirectIndexed, 1, 5 )
		OP_ILLEGAL( 0x52 )
		OP_ILLEGAL( 0x53 )
		OP( 0x54, SKB, 0, 4 )
		OP_ADDR( 0x55, EOR, IndexedZeroX, 1, 4 )
		OP_ADDR( 0x56, LSR, IndexedZeroX, 1, 6 )
		OP_ILLEGAL( 0x57 )
		OP( 0x58, CLI, 0, 2 )
		OP_ADDR( 0x59, EOR, IndexedAbsoluteY, 2, 4 )
		OP( 0x5A, NOP, 0, 2 )
		OP_ILLEGAL( 0x5B )
		OP( 0x5C, SKW, 0, 4 )
		OP_ADDR( 0x5D, EOR, IndexedAbsoluteX, 2, 4 )
		OP_ADDR( 0x5E, LSR, IndexedAbsoluteX, 2, 7 )
		OP_ILLEGAL( 0x5F )
		OP_JMP( 0x60, RTS, 0, 6 )
		OP_ADDR( 0x61, ADC, IndexedIndirect, 1, 6 )
		OP_ILLEGAL( 0x62 )
		OP_ILLEGAL( 0x63 )
		OP( 0x64, SKB, 0, 3 )
		OP_ADDR( 0x65, ADC, Zero, 1, 3 )
		OP_ADDR( 0x66, ROR, Zero, 1, 5 )
		OP_ILLEGAL( 0x67 )
		OP( 0x68, PLA, 0, 4 )
		OP_ADDR( 0x69, ADC, Immediate, 1, 2 )
		OP_ADDR( 0x6A, ROR, Accumulator, 0, 2 )
		OP_ILLEGAL( 0x6B )
		OP_JMP( 0x6C, JMPI, 2, 5 )
		OP_ADDR( 0x6D, ADC, Absolute, 2, 4 )
		OP_ADDR( 0x6E, ROR, Absolute, 2, 6 )
		OP_ILLEGAL( 0x6F )
		OP_JMP( 0x70, BVS, 1, 2 )
		OP_ADDR( 0x71, ADC, IndirectIndexed, 1, 5 )
		OP_ILLEGAL( 0x72 )
		OP_ILLEGAL( 0x73 )
		OP( 0x74, SKB, 0, 4 )
		OP_ADDR( 0x75, ADC, IndexedZeroX, 1, 4 )
		OP_ADDR( 0x76, ROR, IndexedZeroX, 1, 6 )
		OP_ILLEGAL( 0x77 )
		OP( 0x78, SEI, 0, 2 )
		OP_ADDR( 0x79, ADC, IndexedAbsoluteY, 2, 4 )
		OP( 0x7A, NOP, 0, 2 )
		OP_ILLEGAL( 0x7B )
		OP( 0x7C, SKW, 0, 4 )
		OP_ADDR( 0x7D, ADC, IndexedAbsoluteX, 2, 4 )
		OP_ADDR( 0x7E, ROR, IndexedAbsoluteX, 2, 7 )
		OP_ILLEGAL( 0x7F )
		OP( 0x80, SKB, 0, 2 )
		OP_ADDR( 0x81, STA, IndexedIndirect, 1, 6 )
		OP_ILLEGAL( 0x82 )
		OP_ILLEGAL( 0x83 )
		OP_ADDR( 0x84, STY, Zero, 1, 3 )
		OP_ADDR( 0x85, STA, Zero, 1, 3 )
		OP_ADDR( 0x86, STX, Zero, 1, 3 )
		OP_ILLEGAL( 0x87 )
		OP( 0x88, DEY, 0, 2 )
		OP_ILLEGAL( 0x89 )
		OP( 0x8A, TXA, 0, 2 )
		OP_ILLEGAL( 0x8B )
		OP_ADDR( 0x8C, STY, Absolute, 2, 4 )
		OP_ADDR( 0x8D, STA, Absolute, 2, 4 )
		OP_ADDR( 0x8E, STX, Absolute, 2, 4 )
		OP_ILLEGAL( 0x8F )
		OP_JMP( 0x90, BCC, 1, 2 )
		OP_ADDR( 0x91, STA, IndirectIndexed, 1, 6 )
		OP_ILLEGAL( 0x92 )
		OP_ILLEGAL( 0x93 )
		OP_ADDR( 0x94, STY, IndexedZeroX, 1, 4 )
		OP_ADDR( 0x95, STA, IndexedZeroX, 1, 4 )
		OP_ADDR( 0x96, STX, IndexedZeroY, 1, 4 )
		OP_ILLEGAL( 0x97 )
		OP( 0x98, TYA, 0, 2 )
		OP_ADDR( 0x99, STA, IndexedAbsoluteY, 2, 5 )
		OP( 0x9A, TXS, 0, 2 )
		OP_ILLEGAL( 0x9B )
		OP_ILLEGAL( 0x9C )
		OP_ADDR( 0x9D, STA, IndexedAbsoluteX, 2, 5 )
		OP_ILLEGAL( 0x9E )
		OP_ILLEGAL( 0x9F )
		OP_ADDR( 0xA0, LDY, Immediate, 1, 2 )
		OP_ADDR( 0xA1, LDA, IndexedIndirect, 1, 6 )
		OP_ADDR( 0xA2, LDX, Immediate, 1, 2 )
		OP_ILLEGAL( 0xA3 )
		OP_ADDR( 0xA4, LDY, Zero, 1, 3 )
		OP_ADDR( 0xA5, LDA, Zero, 1, 3 )
		OP_ADDR( 0xA6, LDX, Zero, 1, 3 )
		OP_ILLEGAL( 0xA7 )
		OP( 0xA8, TAY, 0, 2 )
		OP_ADDR( 0xA9, LDA, Immediate, 1, 2 )
		OP( 0xAA, TAX, 0, 2 )
		OP_ILLEGAL( 0xAB )
		OP_ADDR( 0xAC, LDY, Absolute, 2, 4 )
		OP_ADDR( 0xAD, LDA, Absolute, 2, 4 )
		OP_ADDR( 0xAE, LDX, Absolute, 2, 4 )
		OP_ILLEGAL( 0xAF )
		OP_JMP( 0xB0, BCS, 1, 2 )
		OP_ADDR( 0xB1, LDA, IndirectIndexed, 1, 5 )
		OP_ILLEGAL( 0xB2 )
		OP_ILLEGAL( 0xB3 )
		OP_ADDR( 0xB4, LDY, IndexedZeroX, 1, 4 )
		OP_ADDR( 0xB5, LDA, IndexedZeroX, 1, 4 )
		OP_ADDR( 0xB6, LDX, IndexedZeroY, 1, 4 )
		OP_ILLEGAL( 0xB7 )
		OP( 0xB8, CLV, 0, 2 )
		OP_ADDR( 0xB9, LDA, IndexedAbsoluteY, 2, 4 )
		OP( 0xBA, TSX, 0, 2 )
		OP_ILLEGAL( 0xBB )
		OP_ADDR( 0xBC, LDY, IndexedAbsoluteX, 2, 4 )
		OP_ADDR( 0xBD, LDA, IndexedAbsoluteX, 2, 4 )
		OP_ADDR( 0xBE, LDX, IndexedAbsoluteY, 2, 4 )
		OP_ILLEGAL( 0xBF )
		OP_ADDR( 0xC0, CPY, Immediate, 1, 2 )
		OP_ADDR( 0xC1, CMP, IndexedIndirect, 1, 6 )
		OP_ILLEGAL( 0xC2 )
		OP_ILLEGAL( 0xC3 )
		OP_ADDR( 0xC4, CPY, Zero, 1, 3 )
		OP_ADDR( 0xC5, CMP, Zero, 1, 3 )
		OP_ADDR( 0xC6, DEC, Zero, 1, 5 )
		OP_ILLEGAL( 0xC7 )
		OP( 0xC8, INY, 0, 2 )
		OP_ADDR( 0xC9, CMP, Immediate, 1, 2 )
		OP( 0xCA, DEX, 0, 2 )
		OP_ILLEGAL( 0xCB )
		OP_ADDR( 0xCC, CPY, Absolute, 2, 4 )
		OP_ADDR( 0xCD, CMP, Absolute, 2, 4 )
		OP_ADDR( 0xCE, DEC, Absolute, 2, 6 )
		OP_ILLEGAL( 0xCF )
		OP_JMP( 0xD0, BNE, 1, 2 )
		OP_ADDR( 0xD1, CMP, IndirectIndexed, 1, 5 )
		OP_ILLEGAL( 0xD2 )
		OP_ILLEGAL( 0xD3 )
		OP( 0xD4, SKB, 0, 4 )
		OP_ADDR( 0xD5, CMP, IndexedZeroX, 1, 4 )
		OP_ADDR( 0xD6, DEC, IndexedZeroX, 1, 6 )
		OP_ILLEGAL( 0xD7 )
		OP( 0xD8, CLD, 0, 2 )
		OP_ADDR( 0xD9, CMP, IndexedAbsoluteY, 2, 4 )
		OP( 0xDA, NOP, 0, 2 )
		OP_ILLEGAL( 0xDB )
		OP( 0xDC, SKW, 0, 4 )
		OP_ADDR( 0xDD, CMP, IndexedAbsoluteX, 2, 4 )
		OP_ADDR( 0xDE, DEC, IndexedAbsoluteX, 2, 7 )
		OP_ILLEGAL( 0xDF )
		OP_ADDR( 0xE0, CPX, Immediate, 1, 2 )
		OP_ADDR( 0xE1, SBC, IndexedIndirect, 1, 6 )
		OP_ILLEGAL( 0xE2 )
		OP_ILLEGAL( 0xE3 )
		OP_ADDR( 0xE4, CPX, Zero, 1, 3 )
		OP_ADDR( 0xE5, SBC, Zero, 1, 3 )
		OP_ADDR( 0xE6, INC, Zero, 1, 5 )
		OP_ILLEGAL( 0xE7 )
		OP( 0xE8, INX, 0, 2 )
		OP_ADDR( 0xE9, SBC, Immediate, 1, 2 )
		OP( 0xEA, NOP, 0, 2 )
		OP_ILLEGAL( 0xEB )
		OP_ADDR( 0xEC, CPX, Absolute, 2, 4 )
		OP_ADDR( 0xED, SBC, Absolute, 2, 4 )
		OP_ADDR( 0xEE, INC, Absolute, 2, 6 )
		OP_ILLEGAL( 0xEF )
		OP_JMP( 0xF0, BEQ, 1, 2 )
		OP_ADDR( 0xF1, SBC, IndirectIndexed, 1, 5 )
		OP_ILLEGAL( 0xF2 )
		OP_ILLEGAL( 0xF3 )
		OP( 0xF4, SKB, 0, 4 )
		OP_ADDR( 0xF5, SBC, IndexedZeroX, 1, 4 )
		OP_ADDR( 0xF6, INC, IndexedZeroX, 1, 6 )
		OP_ILLEGAL( 0xF7 )
		OP( 0xF8, SED, 0, 2 )
		OP_ADDR( 0xF9, SBC, IndexedAbsoluteY, 2, 4 )
		OP( 0xFA, NOP, 0, 2 )
		OP_ILLEGAL( 0xFB )
		OP( 0xFC, SKW, 0, 4 )
		OP_ADDR( 0xFD, SBC, IndexedAbsoluteX, 2, 4 )
		OP_ADDR( 0xFE, INC, IndexedAbsoluteX, 2, 7 )
		OP_ILLEGAL( 0xFF )
	}

	//DEBUG_CPU_LOG
#if DEBUG_ADDR == 1
	DebugPrintRegisters( debugReg, regStr, logFrameCount > 0 );
	DebugPrintFlushInstruction( *system, instrBegin, byteCode, operands, mnemonic, regStr );
#endif // #if DEBUG_ADDR == 1

	return retVal;
}