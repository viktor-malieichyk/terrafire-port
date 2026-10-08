// Post-analysis: create functions in undefined CODE32 gaps that look like real
// Watcom functions, roll back any that disassemble into errors, then apply
// __watcall to every function.
// @category TerraFire
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryBlock;

public class FillGaps extends GhidraScript {
    private boolean isPad(int b) { return b == 0x00 || b == 0x90 || b == 0xcc; }

    private boolean looksLikePrologue(Address a) throws Exception {
        int b0 = getByte(a) & 0xff, b1 = getByte(a.add(1)) & 0xff;
        if (b0 >= 0x50 && b0 <= 0x57) return true;              // push reg
        return (b0 == 0x83 || b0 == 0x81) && b1 == 0xec;          // sub esp, imm
    }

    private boolean followsTerminator(Address a) throws Exception {
        int prev = getByte(a.subtract(1)) & 0xff;
        if (prev == 0xc3 || isPad(prev)) return true;
        Instruction pi = getInstructionContaining(a.subtract(1));
        return pi != null && !pi.getFlowType().hasFallthrough();
    }

    private boolean hasErrorIn(AddressSetView body) {
        var it = currentProgram.getBookmarkManager().getBookmarksIterator("Error");
        while (it.hasNext()) if (body.contains(it.next().getAddress())) return true;
        return false;
    }

    @Override
    public void run() throws Exception {
        MemoryBlock code = currentProgram.getMemory().getBlock("CODE32");
        int created = 0, rejected = 0;
        for (int pass = 0; pass < 5; pass++) {
            int before = created;
            Address a = code.getStart().add(1);
            while (a.compareTo(code.getEnd()) < 0) {
                monitor.checkCancelled();
                boolean undefined = getInstructionContaining(a) == null && getDataContaining(a) == null;
                if (undefined && !isPad(getByte(a) & 0xff) && looksLikePrologue(a) && followsTerminator(a)) {
                    if (disassemble(a)) {
                        Function f = createFunction(a, null);
                        if (f != null && !hasErrorIn(f.getBody())) {
                            created++;
                            a = f.getBody().getMaxAddress().next();
                            continue;
                        }
                        if (f != null) removeFunction(f);
                        Address end = a.add(0x40);
                        clearListing(a, end.compareTo(code.getEnd()) > 0 ? code.getEnd() : end);
                        rejected++;
                    }
                }
                a = a.next();
            }
            analyzeChanges(currentProgram);
            println("gap pass " + pass + ": +" + (created - before));
            if (created == before) break;
        }
        for (Function f : currentProgram.getFunctionManager().getFunctions(true))
            f.setCallingConvention("__watcall");
        println("created " + created + " gap functions, rejected " + rejected);
    }
}
