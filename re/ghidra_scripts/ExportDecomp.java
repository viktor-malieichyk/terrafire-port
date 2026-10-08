// Export decompiled C for every function plus a function index.
// @category TerraFire
import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.*;

public class ExportDecomp extends GhidraScript {
    @Override
    public void run() throws Exception {
        String outDir = getScriptArgs().length > 0 ? getScriptArgs()[0] : ".";
        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);
        try (PrintWriter c = new PrintWriter(new FileWriter(outDir + "/part2_decomp.c"));
             PrintWriter idx = new PrintWriter(new FileWriter(outDir + "/functions.tsv"))) {
            idx.println("address\tname\tsize");
            int n = 0, fail = 0;
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                monitor.checkCancelled();
                idx.printf("%s\t%s\t%d%n", f.getEntryPoint(), f.getName(), f.getBody().getNumAddresses());
                DecompileResults r = ifc.decompileFunction(f, 60, monitor);
                c.printf("%n/* ---- %s @ %s ---- */%n", f.getName(), f.getEntryPoint());
                if (r.decompileCompleted()) {
                    c.println(r.getDecompiledFunction().getC());
                } else {
                    c.println("/* decompile failed: " + r.getErrorMessage() + " */");
                    fail++;
                }
                n++;
            }
            println("exported " + n + " functions, " + fail + " failures");
        }
        ifc.dispose();
    }
}
