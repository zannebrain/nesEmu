
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
#include "6502.h"
#include "NesSystem.h"


uint8_t& PPU::Reg( uint16_t address, uint8_t value )
{
	const uint16_t regNum = ( address - 0x2000 );

	PpuRegWriteFunc regFunc = PpuRegWriteMap[regNum];

	return ( this->*regFunc )( value );
}



uint8_t& PPU::Reg( uint16_t address )
{
	const uint16_t regNum = ( address - 0x2000 );

	PpuRegReadFunc regFunc = PpuRegReadMap[regNum];

	return ( this->*regFunc )();
}


inline void PPU::GenerateNMI()
{
	system->cpu.interruptTriggered = true;
}

inline void PPU::EnablePrinting()
{
#if DEBUG_ADDR == 1
	system->cpu.printToOutput = true;
#endif // #if DEBUG_ADDR == 1
}


inline void PPU::GenerateDMA()
{
	system->cpu.oamInProcess = true;
}


uint8_t PPU::DoDMA( const uint16_t address )
{
//	assert( address == 0 ); // need to handle other case
	memcpy( primaryOAM, &system->GetMemory( Combine( 0x00, static_cast<uint8_t>(address) ) ), 256 );

	return 0;
}



inline bool NesSystem::IsPpuRegister( const uint16_t address )
{
	return ( address >= PpuRegisterBase ) && ( address < PpuRegisterEnd );
}


inline bool NesSystem::IsDMA( const uint16_t address )
{
	// TODO: port technically on CPU
	return ( address == PpuOamDma );
}


inline uint16_t NesSystem::MirrorAddress( const uint16_t address )
{
	if ( IsPpuRegister( address ) )
	{
		return ( address % PPU::RegisterCount ) + PpuRegisterBase;
	}
	else if( IsDMA( address ) )
	{
		return address;
	}
	else if ( ( address >= PhysicalMemorySize ) && ( address < PpuRegisterBase ) )
	{
		assert( 0 );
		return ( address % PhysicalMemorySize );
	}
	else
	{
		return ( address % MemoryWrap );
	}
}


inline uint8_t& NesSystem::GetStack()
{
	return memory[StackBase + cpu.SP];
}


inline uint8_t& NesSystem::GetMemory( const uint16_t address )
{
	if( IsPpuRegister( address ) )
	{
		return ppu.Reg( address );
	}
	else if (IsDMA( address ) )
	{
		return memory[MirrorAddress( address )];
	}
	else
	{
		return memory[MirrorAddress( address )];
	}
}

//test commit
using namespace std;

void RegistersToString( const Cpu6502& cpu, string& regStr )
{
	stringstream sStream;

	sStream << uppercase << "A:" << setfill( '0' ) << setw(2) << hex << static_cast<int>( cpu.A ) << setw(1) << " ";
	sStream << uppercase << "X:" << setfill( '0' ) << setw( 2 ) << hex << static_cast<int>( cpu.X ) << setw( 1 ) << " ";
	sStream << uppercase << "Y:" << setfill( '0' ) << setw( 2 ) << hex << static_cast<int>( cpu.Y ) << setw( 1 ) << " ";
	sStream << uppercase << "P:" << setfill( '0' ) << setw( 2 ) << hex << static_cast<int>( cpu.P ) << setw( 1 ) << " ";
	sStream << uppercase << "SP:" << setfill( '0' ) << setw( 2 ) << hex << static_cast<int>( cpu.SP ) << setw( 1 );
	//sStream << "PC:" << hex << static_cast<int>( cpu.PC ) << " ";
	//sStream << uppercase << " CYC:" << setw( 3 ) << "0\0";

	bitset<8> p( cpu.P );

	//sStream << "\tP: NV-BDIZC" << "\t" << p;

	regStr = sStream.str();
}


inline void Cpu6502::SetFlags( const StatusBit bit, const bool toggleOn )
{
	P &= ~bit;

	if ( !toggleOn )
		return;

	P |= ( bit | STATUS_UNUSED );
}


inline void Cpu6502::SetAluFlags( const uint16_t value )
{
	SetFlags( STATUS_ZERO,		CheckZero( value ) );
	SetFlags( STATUS_NEGATIVE,	CheckSign( value ) );
}


inline bool Cpu6502::CheckSign( const uint16_t checkValue )
{
	return ( checkValue & 0x0080 );
}


inline bool Cpu6502::CheckCarry( const uint16_t checkValue )
{
	return ( checkValue > 0x00ff );
}


inline bool Cpu6502::CheckZero( const uint16_t checkValue )
{
	return ( checkValue == 0 );
}


inline bool Cpu6502::CheckOverflow( const uint16_t src, const uint16_t temp, const uint8_t finalValue )
{
	const uint8_t signedBound = 0x80;
	return CheckSign( finalValue ^ src ) && CheckSign( temp ^ src ) && !CheckCarry( temp );
}


uint8_t Cpu6502::SEC( const InstrParams& params )
{
	SetFlags( STATUS_CARRY, true );

	return 0;
}


uint8_t Cpu6502::SEI( const InstrParams& params )
{
	SetFlags( STATUS_INTERRUPT, true );

	return 0;
}


uint8_t Cpu6502::SED( const InstrParams& params )
{
	SetFlags( STATUS_DECIMAL, true );

	return 0;
}


uint8_t Cpu6502::CLC( const InstrParams& params )
{
	SetFlags( STATUS_CARRY, false );

	return 0;
}


uint8_t Cpu6502::CLI( const InstrParams& params )
{
	SetFlags( STATUS_INTERRUPT, false );

	return 0;
}


uint8_t Cpu6502::CLV( const InstrParams& params )
{
	SetFlags( STATUS_OVERFLOW, false );

	return 0;
}


uint8_t Cpu6502::CLD( const InstrParams& params )
{
	SetFlags( STATUS_DECIMAL, false );

	return 0;
}


inline uint8_t& Cpu6502::Read( const InstrParams& params )
{
	uint32_t address = 0x00;

	const AddrFunction getAddr = params.getAddr;

	uint8_t& M = ( this->*getAddr )( params, address );

	return M;
}


inline void Cpu6502::Write( const InstrParams& params, const uint8_t value )
{
	assert( params.getAddr != &Cpu6502::Immediate );

	uint32_t address = 0x00;

	const AddrFunction getAddr = params.getAddr;

	uint8_t& M = ( this->*getAddr )( params, address );

	// TODO: Check is RAM, otherwise do another GetMem, get will do the routing logic
	// Might need to have a different wrapper that passes the work to different handlers
	// That could help with RAM, I/O regs, and mappers
	// Wait until working on mappers to design
	// Addressing functions will call a lower level RAM function only
	// TODO: Need a memory manager class

	if ( system->IsPpuRegister( address ) )
	{
		system->ppu.Reg( address, value );
	}
	else if ( system->IsDMA( address ) )
	{
		system->ppu.OAMDMA( value );
	}
	else
	{
		M = value;
	}

#if DEBUG_MEM == 1
	system->memoryDebug[address] = value;
#endif // #if DEBUG_MEM == 1
}


uint8_t Cpu6502::CMP( const InstrParams& params )
{
	const uint16_t result = A - Read( params );

	SetFlags( STATUS_CARRY, !CheckCarry( result ) );
	SetAluFlags( result );

	return 0;
}


uint8_t Cpu6502::CPX( const InstrParams& params )
{
	const uint16_t result = X - Read( params );

	SetFlags( STATUS_CARRY, !CheckCarry( result ) );
	SetAluFlags( result );

	return 0;
}


uint8_t Cpu6502::CPY( const InstrParams& params )
{
	const uint16_t result = Y - Read( params );

	SetFlags( STATUS_CARRY, !CheckCarry( result ) );
	SetAluFlags( result );

	return 0;
}


inline uint8_t Cpu6502::AddPageCrossCycles( const uint16_t address )
{
	instructionCycles;
	return 0;
}


inline uint16_t Cpu6502::CombineIndirect( const uint8_t lsb, const uint8_t msb, const uint32_t wrap )
{
	const uint16_t address	= Combine( lsb, msb );
	const uint8_t louint8_t	= system->GetMemory( address % wrap );
	const uint8_t hiuint8_t	= system->GetMemory( ( address + 1 ) % wrap );
	const uint16_t value	= Combine( louint8_t, hiuint8_t );

	return value;
}


uint8_t& Cpu6502::IndexedIndirect( const InstrParams& params, uint32_t& address )
{
	const uint8_t targetAddress = ( params.param0 + X );
	address = CombineIndirect( targetAddress, 0x00, NesSystem::ZeroPageWrap );

	uint8_t& value = system->GetMemory( address );

	DEBUG_ADDR_INDEXED_INDIRECT

	return value;
}


uint8_t& Cpu6502::IndirectIndexed( const InstrParams& params, uint32_t& address )
{
	address = CombineIndirect( params.param0, 0x00, NesSystem::ZeroPageWrap );

	const uint16_t offset = ( address + Y ) % NesSystem::MemoryWrap;

	uint8_t& value = system->GetMemory( offset );

	AddPageCrossCycles( address );

	DEBUG_ADDR_INDIRECT_INDEXED

	return value;
}


uint8_t& Cpu6502::Absolute( const InstrParams& params, uint32_t& address )
{
	address = Combine( params.param0, params.param1 );

	uint8_t& value = system->GetMemory( address );

	DEBUG_ADDR_ABS

	return value;
}


inline uint8_t& Cpu6502::IndexedAbsolute( const InstrParams& params, uint32_t& address, const uint8_t& reg )
{
	const uint16_t targetAddresss = Combine( params.param0, params.param1 );

	address = ( targetAddresss + reg ) % NesSystem::MemoryWrap;

	uint8_t& value = system->GetMemory( address );

	AddPageCrossCycles( address );

	DEBUG_ADDR_INDEXED_ABS

	return value;
}


uint8_t& Cpu6502::IndexedAbsoluteX( const InstrParams& params, uint32_t& address )
{
	return IndexedAbsolute( params, address, X );
}


uint8_t& Cpu6502::IndexedAbsoluteY( const InstrParams& params, uint32_t& address )
{
	return IndexedAbsolute( params, address, Y );
}


inline uint8_t& Cpu6502::Zero( const InstrParams& params, uint32_t& address )
{
	const uint16_t targetAddresss = Combine( params.param0, 0x00 );

	address = ( targetAddresss % NesSystem::ZeroPageWrap );

	uint8_t& value = system->GetMemory( address );

	DEBUG_ADDR_ZERO

	return value;
}


inline uint8_t& Cpu6502::IndexedZero( const InstrParams& params, uint32_t& address, const uint8_t& reg )
{
	const uint16_t targetAddresss = Combine( params.param0, 0x00 );

	address = ( targetAddresss + reg ) % NesSystem::ZeroPageWrap;

	uint8_t& value = system->GetMemory( address );

	DEBUG_ADDR_INDEXED_ZERO

	return value;
}


uint8_t& Cpu6502::IndexedZeroX( const InstrParams& params, uint32_t& address )
{
	return IndexedZero( params, address, X );
}


uint8_t& Cpu6502::IndexedZeroY( const InstrParams& params, uint32_t& address )
{
	return IndexedZero( params, address, Y );
}


uint8_t& Cpu6502::Immediate( const InstrParams& params, uint32_t& address )
{
	// This is a bit dirty, but necessary to maintain uniformity, asserts have been placed at lhs usages for now
	uint8_t& value = const_cast< InstrParams& >( params ).param0;

	address = Cpu6502::InvalidAddress;

	DEBUG_ADDR_IMMEDIATE

	return value;
}


uint8_t& Cpu6502::Accumulator( const InstrParams& params, uint32_t& address )
{
	address = Cpu6502::InvalidAddress;

	DEBUG_ADDR_ACCUMULATOR

	return A;
}


uint8_t Cpu6502::LDA( const InstrParams& params )
{
	A = Read( params );

	SetAluFlags( A );

	return 0;
}


uint8_t Cpu6502::LDX( const InstrParams& params )
{
	X = Read( params );

	SetAluFlags( X );

	return 0;
}


uint8_t Cpu6502::LDY( const InstrParams& params )
{
	Y = Read( params );

	SetAluFlags( Y );

	return 0;
}


uint8_t Cpu6502::STA( const InstrParams& params )
{
	Write( params, A );

	return 0;
}


inline uint8_t Cpu6502::STX( const InstrParams& params )
{
	Write( params, X );

	return 0;
}


uint8_t Cpu6502::STY( const InstrParams& params )
{
	Write( params, Y );

	return 0;
}


uint8_t Cpu6502::TXS( const InstrParams& params )
{
	SP = X;

	return 0;
}


uint8_t Cpu6502::TXA( const InstrParams& params )
{
	A = X;
	SetAluFlags( A );

	return 0;
}


uint8_t Cpu6502::TYA( const InstrParams& params )
{
	A = Y;
	SetAluFlags( A );

	return 0;
}


uint8_t Cpu6502::TAX( const InstrParams& params )
{
	X = A;
	SetAluFlags( X );

	return 0;
}


uint8_t Cpu6502::TAY( const InstrParams& params )
{
	Y = A;
	SetAluFlags( Y );

	return 0;
}


uint8_t Cpu6502::TSX( const InstrParams& params )
{
	X = SP;
	SetAluFlags( X );

	return 0;
}


uint8_t Cpu6502::ADC( const InstrParams& params )
{
	// http://nesdev.com/6502.txt, INSTRUCTION OPERATION - ADC
	const uint8_t M		= Read( params );
	const uint16_t src		= A;
	const uint16_t carry	= ( P & STATUS_CARRY ) ? 1 : 0;
	const uint16_t temp		= A + M + carry;

	A = ( temp & 0xFF );

	SetFlags( STATUS_ZERO,		CheckZero( temp ) );
	SetFlags( STATUS_OVERFLOW,	CheckOverflow( M, temp, A ) );
	SetAluFlags( A );
	SetFlags( STATUS_CARRY,		( temp > 0xFF ) );

	return 0;
}


uint8_t Cpu6502::SBC( const InstrParams& params )
{
	const uint8_t& M		= Read( params );
	const uint16_t carry	= ( P & STATUS_CARRY ) ? 0 : 1;
	const uint16_t result	= A - M - carry;

	SetAluFlags( result );
	SetFlags( STATUS_OVERFLOW,	CheckSign( A ^ result ) && CheckSign( A ^ M ) );
	SetFlags( STATUS_CARRY,		!CheckCarry( result ) );

	A = result & 0xFF;

	return 0;
}


uint8_t Cpu6502::INX( const InstrParams& params )
{
	X++;
	SetAluFlags( X );

	return 0;
}


uint8_t Cpu6502::INY( const InstrParams& params )
{
	Y++;
	SetAluFlags( Y );

	return 0;
}


uint8_t Cpu6502::DEX( const InstrParams& params )
{
	X--;
	SetAluFlags( X );

	return 0;
}


uint8_t Cpu6502::DEY( const InstrParams& params )
{
	Y--;
	SetAluFlags( Y );

	return 0;
}


uint8_t Cpu6502::INC( const InstrParams& params )
{
	const uint8_t result = Read( params ) + 1;

	Write( params, result );

	SetAluFlags( result );

	return 0;
}


uint8_t Cpu6502::DEC( const InstrParams& params )
{
	const uint8_t result = Read( params ) - 1;

	Write( params, result );

	SetAluFlags( result );

	return 0;
}


void Cpu6502::Push( const uint8_t value )
{
	system->GetStack() = value;
	SP--;
}


void Cpu6502::Pushuint16_t( const uint16_t value )
{
	Push( ( value >> 8 ) & 0xFF );
	Push( value & 0xFF );
}


uint8_t Cpu6502::Pull()
{
	SP++;
	return system->GetStack();
}


uint16_t Cpu6502::Pulluint16_t()
{
	const uint8_t louint8_t = Pull();
	const uint8_t hiuint8_t = Pull();

	return Combine( louint8_t, hiuint8_t );
}


uint8_t Cpu6502::PHP( const InstrParams& params )
{
	Push( P | STATUS_UNUSED | STATUS_BREAK );

	return 0;
}


uint8_t Cpu6502::PHA( const InstrParams& params )
{
	Push( A );

	return 0;
}


uint8_t Cpu6502::PLA( const InstrParams& params )
{
	A = Pull();

	SetAluFlags( A );

	return 0;
}


uint8_t Cpu6502::PLP( const InstrParams& params )
{
	// https://wiki.nesdev.com/w/index.php/Status_flags
	const uint8_t status = ~STATUS_BREAK & Pull();
	P = status | ( P & STATUS_BREAK ) | STATUS_UNUSED;

	return 0;
}


uint8_t Cpu6502::NOP( const InstrParams& params )
{
	return 0;
}


uint8_t Cpu6502::ASL( const InstrParams& params )
{
	const uint8_t& M = Read( params );

	SetFlags( STATUS_CARRY,		M & 0x80 );
	Write( params, M << 1 );
	SetAluFlags( M );

	return 0;
}


uint8_t Cpu6502::LSR( const InstrParams& params )
{
	const uint8_t& M = Read( params );

	SetFlags( STATUS_CARRY,		M & 0x01 );
	Write( params, M >> 1 );
	SetAluFlags( M );

	return 0;
}


uint8_t Cpu6502::AND( const InstrParams& params )
{
	A &= Read( params );

	SetAluFlags( A );

	return 0;
}


uint8_t Cpu6502::BIT( const InstrParams& params )
{
	const uint8_t& M = Read( params );

	SetFlags( STATUS_ZERO,		!( A & M ) );
	SetFlags( STATUS_NEGATIVE,	CheckSign( M ) );
	SetFlags( STATUS_OVERFLOW,	M & 0x40 );

	return 0;
}


uint8_t Cpu6502::EOR( const InstrParams& params )
{
	A ^= Read( params );

	SetAluFlags( A );

	return 0;
}


uint8_t Cpu6502::ORA( const InstrParams& params )
{
	A |= Read( params );

	SetAluFlags( A );

	return 0;
}


uint8_t Cpu6502::JMP( const InstrParams& params )
{
	PC = Combine( params.param0, params.param1 );

	DEBUG_ADDR_JMP

	return 0;
}


uint8_t Cpu6502::JMPI( const InstrParams& params )
{
	const uint16_t addr0 = Combine( params.param0, params.param1 );

	// Hardware bug - http://wiki.nesdev.com/w/index.php/Errata
	if ( ( addr0 & 0xff ) == 0xff )
	{
		const uint16_t addr1 = Combine( 0x00, params.param1 );

		PC = ( Combine( system->GetMemory( addr0 ), system->GetMemory( addr1 ) ) );
	}
	else
	{
		PC = ( Combine( system->GetMemory( addr0 ), system->GetMemory( addr0 + 1 ) ) );
	}

	DEBUG_ADDR_JMPI

	return 0;
}


uint8_t Cpu6502::JSR( const InstrParams& params )
{
	uint16_t retAddr = PC - 1;

	Push( ( retAddr >> 8 ) & 0xFF );
	Push( retAddr & 0xFF );

	PC = Combine( params.param0, params.param1 );

	DEBUG_ADDR_JSR

	return 0;
}


uint8_t Cpu6502::BRK( const InstrParams& params )
{
	SetFlags( STATUS_BREAK, true );

	interruptTriggered = true;

	return 0;
}


uint8_t Cpu6502::RTS( const InstrParams& params )
{
	const uint8_t louint8_t = Pull();
	const uint8_t hiuint8_t = Pull();

	PC = 1 + Combine( louint8_t, hiuint8_t );

	return 0;
}


uint8_t Cpu6502::RTI( const InstrParams& params )
{
	PLP( params );

	const uint8_t louint8_t = Pull();
	const uint8_t hiuint8_t = Pull();

	PC	= Combine( louint8_t, hiuint8_t );

	return 0;
}



inline uint8_t Cpu6502::Branch( const InstrParams& params, const bool takeBranch )
{
	const uint16_t branchedPC = PC + static_cast< int8_t >( params.param0 );

	uint8_t cycles = 0;

	if ( takeBranch )
	{
		PC = branchedPC;
	}
	else
	{
		++cycles;
	}

	DEBUG_ADDR_BRANCH

	return ( cycles + AddPageCrossCycles( branchedPC ) );
}


uint8_t Cpu6502::BMI( const InstrParams& params )
{
	const uint8_t cycles = Branch( params, ( P & STATUS_NEGATIVE ) );

	return ( cycles + 0 );
}


uint8_t Cpu6502::BVS( const InstrParams& params )
{
	const uint8_t cycles = Branch( params, ( P & STATUS_OVERFLOW ) );

	return ( cycles + 0 );
}


uint8_t Cpu6502::BCS( const InstrParams& params )
{
	const uint8_t cycles = Branch( params, ( P & STATUS_CARRY ) );

	return ( cycles + 0 );
}


uint8_t Cpu6502::BEQ( const InstrParams& params )
{
	const uint8_t cycles = Branch( params, ( P & STATUS_ZERO ) );

	return ( cycles + 0 );
}


uint8_t Cpu6502::BPL( const InstrParams& params )
{
	const uint8_t cycles = Branch( params, !( P & STATUS_NEGATIVE ) );

	return ( cycles + 0 );
}


uint8_t Cpu6502::BVC( const InstrParams& params )
{
	const uint8_t cycles = Branch( params, !( P & STATUS_OVERFLOW ) );

	return ( cycles + 0 );
}


uint8_t Cpu6502::BCC( const InstrParams& params )
{
	const uint8_t cycles = Branch( params, !( P & STATUS_CARRY ) );

	return ( cycles + 0 );
}


uint8_t Cpu6502::BNE( const InstrParams& params )
{
	const uint8_t cycles = Branch( params, !( P & STATUS_ZERO ) );

	return ( cycles + 0 );
}


uint8_t Cpu6502::ROL( const InstrParams& params )
{
	uint16_t temp = Read( params ) << 1;
	temp = ( P & STATUS_CARRY ) ? temp | 0x0001 : temp;
	
	SetFlags( STATUS_CARRY, CheckCarry( temp ) );
	
	temp &= 0xFF;

	SetAluFlags( temp );

	Read( params ) = static_cast< uint8_t >( temp & 0xFF );

	return 0;
}


uint8_t Cpu6502::ROR( const InstrParams& params )
{
	uint16_t temp = ( P & STATUS_CARRY ) ? Read( params ) | 0x0100 : Read( params );
	
	SetFlags( STATUS_CARRY, temp & 0x01 );

	temp >>= 1;

	SetAluFlags( temp );

	Read( params ) = static_cast< uint8_t >( temp & 0xFF );

	return 0;
}


uint8_t Cpu6502::NMI( const InstrParams& params )
{
	Pushuint16_t( PC - 1 );
	// http://wiki.nesdev.com/w/index.php/CPU_status_flag_behavior
	Push( P | STATUS_BREAK ); 

	SetFlags( STATUS_INTERRUPT, true );

	PC = nmiVector;

	return 0;
}


uint8_t Cpu6502::IRQ( const InstrParams& params )
{
	return NMI( params );
}


uint8_t Cpu6502::Illegal( const InstrParams& params )
{
	//assert( 0 );

	return 0;
}


bool Cpu6502::IsIllegalOp( const uint8_t opCode )
{
	// These are explicitly excluded since the instruction map code will generate garbage for these ops
	const uint8_t illegalOpFormat = 0x03;
	const uint8_t numIllegalOps = 23;
	static const uint8_t illegalOps[numIllegalOps]
	{
		0x02, 0x22, 0x34, 0x3C, 0x42, 0x44, 0x54, 0x5C,
		0x62, 0x64, 0x74, 0x7C, 0x80, 0x82, 0x89, 0x9C,
		0x9E, 0xC2, 0xD4, 0xDC, 0xE2, 0xF4, 0xFC,
	};


	if ( ( opCode & illegalOpFormat ) == illegalOpFormat )
		return true;

	for ( uint8_t op = 0; op < numIllegalOps; ++op )
	{
		if ( opCode == illegalOps[op] )
			return true;
	}

	return false;
}


inline cpuCycle_t Cpu6502::Exec()
{
#if DEBUG_ADDR == 1
	static bool enablePrinting = true;

	debugAddr.str( std::string() );
	string regStr;
	RegistersToString( *this, regStr );
#endif // #if DEBUG_ADDR == 1

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

#if DEBUG_MODE == 1
	if ( PC == forceStopAddr )
	{
		forceStop = true;
		return cpuCycle_t(0);
	}
#endif // #if DEBUG_MODE == 1

	uint8_t curbyte = system->GetMemory( instrBegin );

	PC++;

	InstrParams params;

	if ( resetTriggered )
	{
	}

	if ( interruptTriggered )
	{
		NMI( params );

		interruptTriggered = false;

		curbyte = system->GetMemory( PC );

		return cpuCycle_t( 0 );
	}

	const InstructionMapTuple& pair = InstructionMap[curbyte];

	const uint8_t operands = pair.operands;

	if ( operands >= 1 )
	{
		params.param0 = system->GetMemory( PC );
	}

	if ( operands == 2 )
	{
		params.param1 = system->GetMemory( PC + 1 );
	}

	PC += operands;

	Instruction instruction = pair.instr;

	params.getAddr = pair.addrFunc;

	( this->*instruction )( params );

	instructionCycles += cpuCycle_t( pair.cycles );

	DEBUG_CPU_LOG

	return instructionCycles;
}


inline bool Cpu6502::Step( const cpuCycle_t nextCycle )
{
	while ( ( cycle < nextCycle ) && !forceStop )
	{
		cycle += cpuCycle_t(Exec());
	}

	return !forceStop;
}





bool NesSystem::Run( const masterCycles_t nextCycle )
{
#if DEBUG_ADDR == 1
	cpu.logFile.open( "tomTendo.log" );
#endif // #if DEBUG_ADDR == 1

	bool isRunning = true;

	static const masterCycles_t ticks( CpuClockDivide );

//	cout << "Start CPU Cycle:" << cpu.cycle.count() << endl;

#if DEBUG_MODE == 1
	auto start = chrono::steady_clock::now();
#endif // #if DEBUG_MODE == 1

	//CHECK WRAP AROUND LOGIC
	while ( ( sysCycles < nextCycle ) && isRunning )
	{
		sysCycles += ticks;

		isRunning = cpu.Step( chrono::duration_cast<cpuCycle_t>( sysCycles ) );
		ppu.Step( chrono::duration_cast<ppuCycle_t>( sysCycles ) );
	}

#if DEBUG_MODE == 1
	auto end = chrono::steady_clock::now();

	auto elapsed = end - start;
	auto dur = chrono::duration <double, milli>( elapsed ).count();

//	cout << "Elapsed:" << dur << ": Cycles: " << cpu.cycle.count() << endl;
#endif // #if DEBUG_MODE == 1

	cpu.cycle -= chrono::duration_cast<cpuCycle_t>( sysCycles );
	ppu.cycle -= chrono::duration_cast<ppuCycle_t>( sysCycles );
	sysCycles -= nextCycle;

#if DEBUG_ADDR == 1
	cpu.logFile.close();
#endif // #if DEBUG_ADDR == 1

	return isRunning;
}