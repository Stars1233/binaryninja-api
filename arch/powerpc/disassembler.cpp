

#include <string>
#include <vector>

#include "binaryninjaapi.h"

#define MYLOG(...) while(0);
// #define MYLOG BinaryNinja::LogWarn
// #define MYLOG printf

using namespace std;
using namespace BinaryNinja; // for ::LogDebug, etc.

#include "decode/decode.h"
#include "disassembler.h"
#include "util.h"

bool FillInstruction(Instruction* instruction, const uint8_t* data, size_t length, uint64_t address, uint32_t extraFlags = 0, uint32_t decodeFlags = DECODE_FLAGS_PPC64, BNEndianness endian=LittleEndian)
{
	instruction->numBytes = length;
	switch (instruction->numBytes)
	{
	case 2:
	{
		uint16_t word16 = *(const uint16_t *) data;
		// VLE is always big-endian
		word16 = bswap16(word16);

		return Decompose16(instruction, word16, address, decodeFlags | extraFlags);
	}

	case 4:
	{
		uint32_t word32 = *(const uint32_t *) data;

		// VLE is always big endian
		if (((decodeFlags & DECODE_FLAGS_VLE) != 0) || endian == BigEndian)
			word32 = bswap32(word32);

		return Decompose32(instruction, word32, address, decodeFlags | extraFlags);
	}

	default:
		MYLOG("FillInstruction: unrecognized length %d", length);
		LogWarn("FillInstruction: unrecognized length %d", length);
		return false;
	}
}

bool PushOperandTokens(string& result, const Operand* op)
{
	char buf[32];
	switch (op->cls)
	{
		case PPC_OP_REG_CRFD_IMPLY0:
		case PPC_OP_REG_CRFS_IMPLY0:
			if (op->reg == PPC_REG_CRF0)
				return false;

			result += PowerPCRegisterName(op->reg);

			break;

		case PPC_OP_REG_RA:
		case PPC_OP_REG_RB:
		case PPC_OP_REG_RD:
		case PPC_OP_REG_RS:
		case PPC_OP_REG_FRA:
		case PPC_OP_REG_FRB:
		case PPC_OP_REG_FRC:
		case PPC_OP_REG_FRD:
		case PPC_OP_REG_FRS:
		case PPC_OP_REG_CRFD:
		case PPC_OP_REG_CRFS:
		case PPC_OP_REG_AV_VA:
		case PPC_OP_REG_AV_VB:
		case PPC_OP_REG_AV_VC:
		case PPC_OP_REG_AV_VD:
		case PPC_OP_REG_AV_VS:
		case PPC_OP_REG_VSX_RA:
		case PPC_OP_REG_VSX_RA_DWORD0:
		case PPC_OP_REG_VSX_RB:
		case PPC_OP_REG_VSX_RB_DWORD0:
		case PPC_OP_REG_VSX_RC:
		case PPC_OP_REG_VSX_RC_DWORD0:
		case PPC_OP_REG_VSX_RD:
		case PPC_OP_REG_VSX_RD_DWORD0:
		case PPC_OP_REG_VSX_RS:
		case PPC_OP_REG_VSX_RS_DWORD0:
			result += PowerPCRegisterName(op->reg);
			break;

		case PPC_OP_UIMM:
			snprintf(buf, sizeof(buf), "0x%" PRIx64, op->uimm);
			result += buf;
			break;

		case PPC_OP_SIMM:
			if (op->simm < 0 && op->simm > -0x10000)
				snprintf(buf, sizeof(buf), "-0x%llx", -op->simm);
			else
				snprintf(buf, sizeof(buf), "0x%llx", op->simm);
			result += buf;
			break;

		case PPC_OP_LABEL:
			snprintf(buf, sizeof(buf), "0x%llx", op->label);
			result += buf;
			break;

		case PPC_OP_CRBIT_A:
		case PPC_OP_CRBIT_B:
		case PPC_OP_CRBIT_D:
			result += GetCRBitName(op->crbit);
			break;

		case PPC_OP_MEM_RA:
			// eg: lwz r11, 8(r11)
			//
			// TODO: it would be nice to have the option to print these
			//       in hex; printed in decimal now for backwards compatibility
			snprintf(buf, sizeof(buf), "%d", op->mem.offset);
			result += buf;

			result += "(";
			if (op->mem.reg == PPC_REG_GPR0)
				result += "0";
			else
				result += PowerPCRegisterName(op->mem.reg);
			result += ")";
			break;

		default:
			//MYLOG("pushing a ???\n");
			result += "???";
	}

	return true;
}

int disassemble(uint8_t *data, uint32_t addr, string& result)
{
	int rc = -1;

	char buf[32];
	size_t strlenMnem;
	Instruction instruction;
	const char* mnemonic = NULL;
	size_t len = 4;
	uint32_t decodeFlags = DECODE_FLAGS_PPC64;

	//MYLOG("%s()\n", __func__);
	size_t instructionLength = GetInstructionLength(data, len, decodeFlags);
	if (instructionLength == 0)
	{
		MYLOG("ERROR: not enough bytes for instruction\n");
		return false;
	}

	len = instructionLength;
	if (!FillInstruction(&instruction, data, instructionLength, addr))
	{
		MYLOG("ERROR: FillInstruction()\n");
		return -1;
	}

	/* mnemonic */
	mnemonic = GetMnemonic(&instruction);

	result = mnemonic;
	if (instruction.numOperands > 0)
		result += " ";

	OperandsList operand_list;
	memset(&operand_list, 0, sizeof(operand_list));

	// The default is to just copy every operand, to simplify the
	// alternate code path for special cases
	operand_list.numOperands = instruction.numOperands;
	for (int i = 0; i < instruction.numOperands; ++i)
	{
		operand_list.operands[i] = instruction.operands[i];
	}

	switch(instruction.id)
	{
	case PPC_ID_BCx:
		FillBcxOperands(&operand_list, &instruction);
		break;
	case PPC_ID_BCCTRx:
		FillBcctrxOperands(&operand_list, &instruction);
		break;
	case PPC_ID_BCLRx:
		FillBclrxOperands(&operand_list, &instruction);
		break;
	case PPC_ID_VLE_SE_BC:
		FillVle16BcOperands(&operand_list, &instruction);
		break;
	case PPC_ID_VLE_E_BCx:
		FillVle32BcxOperands(&operand_list, &instruction);
		break;
	default:
		// Already copied by default
		;
	}

	for (int i = 0; i < operand_list.numOperands; ++i)
	{
		Operand* op = &(operand_list.operands[i]);
		bool was_pushed = PushOperandTokens(result, op);

		if (was_pushed && i < operand_list.numOperands - 1)
		{
			//MYLOG("pushing a comma\n");
			result += ", ";
		}
	}
	return 0;
}

