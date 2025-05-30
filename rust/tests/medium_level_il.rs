use binaryninja::binary_view::BinaryViewExt;
use binaryninja::headless::Session;
use binaryninja::medium_level_il::{
    MediumLevelExpressionIndex, MediumLevelILInstructionKind, MediumLevelILLiftedInstructionKind,
    MediumLevelInstructionIndex,
};
use std::path::PathBuf;

#[test]
fn test_mlil_info() {
    let _session = Session::new().expect("Failed to initialize session");
    let out_dir = env!("OUT_DIR").parse::<PathBuf>().unwrap();
    let view = binaryninja::load(out_dir.join("atox.obj")).expect("Failed to create view");
    let image_base = view.original_image_base();

    let entry_function = view.entry_point_function().unwrap();
    let mlil_function = entry_function.medium_level_il().unwrap();
    let mlil_basic_blocks = mlil_function.basic_blocks();
    let mut mlil_basic_block_iter = mlil_basic_blocks.iter();
    let first_basic_block = mlil_basic_block_iter.next().unwrap();
    let mut mlil_instr_iter = first_basic_block.iter();

    // 0 @ 00025f10  (MLIL_SET_VAR.d edi_1 = (MLIL_VAR.d edi))
    let instr_0 = mlil_instr_iter.next().unwrap();
    assert_eq!(instr_0.instr_index, MediumLevelInstructionIndex(0));
    assert_eq!(instr_0.expr_index, MediumLevelExpressionIndex(1));
    assert_eq!(instr_0.address, image_base + 0x00025f10);
    println!("{:?}", instr_0.kind);
    match instr_0.kind {
        MediumLevelILInstructionKind::SetVar(op) => {
            assert_eq!(op.dest.index, 524288);
            assert_eq!(op.src, MediumLevelExpressionIndex(0));
        }
        _ => panic!("Expected SetVar"),
    }
    // 1 @ 00025f15  (MLIL_SET_VAR.d eax = (MLIL_VAR.d arg1))
    let instr_1 = mlil_instr_iter.next().unwrap();
    assert_eq!(instr_1.instr_index, MediumLevelInstructionIndex(1));
    assert_eq!(instr_1.expr_index, MediumLevelExpressionIndex(3));
    assert_eq!(instr_1.address, image_base + 0x00025f15);
    println!("{:?}", instr_1.kind);
    match instr_1.kind {
        MediumLevelILInstructionKind::SetVar(op) => {
            assert_eq!(op.dest.index, 5);
            assert_eq!(op.src, MediumLevelExpressionIndex(2));
        }
        _ => panic!("Expected SetVar"),
    }
    // 2 @ 00025f18  (MLIL_SET_VAR.d var_8 = (MLIL_VAR.d eax))
    let instr_2 = mlil_instr_iter.next().unwrap();
    assert_eq!(instr_2.instr_index, MediumLevelInstructionIndex(2));
    assert_eq!(instr_2.expr_index, MediumLevelExpressionIndex(5));
    assert_eq!(instr_2.address, image_base + 0x00025f18);
    println!("{:?}", instr_2.kind);
    match instr_2.kind {
        MediumLevelILInstructionKind::SetVar(op) => {
            assert_eq!(op.dest.index, 8);
            assert_eq!(op.src, MediumLevelExpressionIndex(4));
        }
        _ => panic!("Expected SetVar"),
    }
    // 3 @ 00025f19  (MLIL_CALL eax_1 = (MLIL_CONST_PTR.d __crt_interlocked_read_32)((MLIL_VAR.d var_8)))
    let instr_3 = mlil_instr_iter.next().unwrap();
    assert_eq!(instr_3.instr_index, MediumLevelInstructionIndex(3));
    assert_eq!(instr_3.expr_index, MediumLevelExpressionIndex(10));
    assert_eq!(instr_3.address, image_base + 0x00025f19);
    println!("{:?}", instr_3.kind);
    match instr_3.kind {
        MediumLevelILInstructionKind::Call(op) => {
            assert_eq!(op.first_output, 8);
            assert_eq!(op.num_outputs, 1);
            assert_eq!(op.dest, MediumLevelExpressionIndex(7));
            assert_eq!(op.first_param, 9);
            assert_eq!(op.num_params, 1);
        }
        _ => panic!("Expected Call"),
    }
    match instr_3.lift().kind {
        MediumLevelILLiftedInstructionKind::Call(lifted_call) => {
            assert_eq!(lifted_call.dest.expr_index, MediumLevelExpressionIndex(7));
            assert_eq!(lifted_call.output.len(), 1);
            assert_eq!(lifted_call.params.len(), 1);
        }
        _ => panic!("Expected Call"),
    }
    // 4 @ 00025f22  (MLIL_RET return (MLIL_VAR.d eax_1))
    let instr_4 = mlil_instr_iter.next().unwrap();
    assert_eq!(instr_4.instr_index, MediumLevelInstructionIndex(4));
    assert_eq!(instr_4.expr_index, MediumLevelExpressionIndex(13));
    assert_eq!(instr_4.address, image_base + 0x00025f22);
    println!("{:?}", instr_4.kind);
    match instr_4.kind {
        MediumLevelILInstructionKind::Ret(op) => {
            assert_eq!(op.first_operand, 12);
            assert_eq!(op.num_operands, 1);
        }
        _ => panic!("Expected Ret"),
    }
}

#[test]
fn test_mlil_basic_blocks() {
    let _session = Session::new().expect("Failed to initialize session");
    let out_dir = env!("OUT_DIR").parse::<PathBuf>().unwrap();
    let view = binaryninja::load(out_dir.join("atox.obj")).expect("Failed to create view");

    // Make sure that all basic blocks are correctly associated with the instruction.
    for func in &view.functions() {
        let mlil_function = func.medium_level_il().expect("Failed to get MLIL");
        for mlil_basic_block in &mlil_function.basic_blocks() {
            for instr in mlil_basic_block.iter() {
                let instr_basic_block = instr
                    .basic_block()
                    .expect("Instruction without basic block");
                assert_eq!(instr_basic_block, mlil_basic_block.to_owned());
            }
        }
    }
}
