from binaryninja import (ConstantRenderer, PointerType, InstructionTextToken, InstructionTextTokenType, DataBuffer)


class EncodedStringConstantRenderer(ConstantRenderer):
    renderer_name = "encoded_strings"
    decoders = {
        "xor_encoded": lambda encoded, key: encoded ^ key,
        "sub_encoded": lambda encoded, key: (encoded - key) & 0xff,
        "add_encoded": lambda encoded, key: (encoded + key) & 0xff
    }

    def is_valid_for_type(self, func, type):
        if not isinstance(type, PointerType):
            return False
        for name in self.__class__.decoders.keys():
            if name in type.target.attributes:
                return True
        return False

    def render_constant_pointer(self, instr, type, val, tokens, settings, precedence):
        if not isinstance(type, PointerType):
            return False

        values = None
        decoder = None
        for name in self.__class__.decoders.keys():
            if name in type.target.attributes:
                try:
                    values = bytes.fromhex(type.target.attributes[name])
                    decoder = self.__class__.decoders[name]
                except:
                    return False
        if values is None or decoder is None:
            return False

        encoded_null = "encoded_null" in type.target.attributes

        result = b""
        i = 0
        while True:
            byte = instr.function.view.read(val + i, 1)
            if len(byte) != 1:
                return False
            if not encoded_null and byte[0] == 0:
                break
            byte = decoder(byte[0], values[i % len(values)])
            if byte == 0:
                break
            result += bytes([byte])
            i += 1

        tokens.append(InstructionTextToken(InstructionTextTokenType.BraceToken, "\""))
        tokens.append(InstructionTextToken(InstructionTextTokenType.StringToken, DataBuffer(result).escape()))
        tokens.append(InstructionTextToken(InstructionTextTokenType.BraceToken, "\"_enc"))
        return True


EncodedStringConstantRenderer().register()
