// Pre-analysis setup for a flattened LE image produced by le_flatten.py:
// memory blocks, Watcom register calling convention, entry + relocation seeds.
// Script args: <path to .coderefs.txt>
// @category TerraFire
import ghidra.app.script.GhidraScript;
import ghidra.program.database.SpecExtension;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;

public class LeSetup extends GhidraScript {
    static final String WATCALL =
        "<prototype name=\"__watcall\" extrapop=\"unknown\" stackshift=\"4\">" +
        " <input>" +
        "  <pentry minsize=\"1\" maxsize=\"4\"><register name=\"EAX\"/></pentry>" +
        "  <pentry minsize=\"1\" maxsize=\"4\"><register name=\"EDX\"/></pentry>" +
        "  <pentry minsize=\"1\" maxsize=\"4\"><register name=\"EBX\"/></pentry>" +
        "  <pentry minsize=\"1\" maxsize=\"4\"><register name=\"ECX\"/></pentry>" +
        "  <pentry minsize=\"1\" maxsize=\"500\" align=\"4\"><addr offset=\"4\" space=\"stack\"/></pentry>" +
        " </input>" +
        " <output>" +
        "  <pentry minsize=\"4\" maxsize=\"10\" metatype=\"float\"><register name=\"ST0\"/></pentry>" +
        "  <pentry minsize=\"1\" maxsize=\"4\"><register name=\"EAX\"/></pentry>" +
        " </output>" +
        " <unaffected>" +
        "  <register name=\"EBX\"/><register name=\"ECX\"/><register name=\"EDX\"/>" +
        "  <register name=\"ESI\"/><register name=\"EDI\"/><register name=\"EBP\"/>" +
        "  <register name=\"ESP\"/><register name=\"DF\"/>" +
        " </unaffected>" +
        " <killedbycall><register name=\"EAX\"/></killedbycall>" +
        "</prototype>";

    @Override
    public void run() throws Exception {
        new SpecExtension(currentProgram).addReplaceCompilerSpecExtension(WATCALL, monitor);

        Memory mem = currentProgram.getMemory();
        Address dataStart = toAddr(0x40000);
        mem.split(mem.getBlock(toAddr(0x10000)), dataStart);
        MemoryBlock code = mem.getBlock(toAddr(0x10000));
        MemoryBlock data = mem.getBlock(dataStart);
        code.setName("CODE32");
        code.setPermissions(true, false, true);
        data.setName("DATA32");
        data.setPermissions(true, true, false);

        Address entry = toAddr(0x315c8);
        currentProgram.getSymbolTable().addExternalEntryPoint(entry);
        disassemble(entry);
        createFunction(entry, "_cstart_");

        int seeded = 0;
        for (String line : Files.readAllLines(Paths.get(getScriptArgs()[0]))) {
            Address a = toAddr(Long.decode(line.trim()));
            if (getInstructionAt(a) == null && disassemble(a) && getFunctionAt(a) == null) {
                if (createFunction(a, null) != null) seeded++;
            }
        }
        println("seeded " + seeded + " functions from relocation targets");
    }
}
