// Recover Watcom register parameters by bottom-up fixpoint.
//
// Watcom passes args in EAX/EDX/EBX/ECX but those registers are also
// callee-saved, so the decompiler can't tell a forwarded argument from a
// register that merely survives a call. Starting every function with a
// locked empty parameter list stops callers leaking registers into callee
// signatures; each pass then commits only the registers a function really
// reads, and repeats until no signature changes.
// @category TerraFire
import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.DataType;
import ghidra.program.model.listing.*;
import ghidra.program.model.listing.Function.FunctionUpdateType;
import ghidra.program.model.pcode.*;
import ghidra.program.model.symbol.SourceType;
import java.util.*;

public class FixParams extends GhidraScript {
    private static String key(List<Parameter> ps) {
        StringBuilder sb = new StringBuilder();
        for (Parameter p : ps) sb.append(p.getVariableStorage()).append(';');
        return sb.toString();
    }

    private void setParams(Function f, List<Parameter> ps) throws Exception {
        f.updateFunction("__watcall", null, ps, FunctionUpdateType.CUSTOM_STORAGE,
                         true, SourceType.ANALYSIS);
        f.setReturnType(DataType.DEFAULT, SourceType.ANALYSIS);  // undefined = output stays unlocked
    }

    @Override
    public void run() throws Exception {
        FunctionManager fm = currentProgram.getFunctionManager();
        Map<Function, String> sig = new HashMap<>();
        for (Function f : fm.getFunctions(true)) {
            setParams(f, new ArrayList<>());
            sig.put(f, "");
        }

        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);
        for (int pass = 1; pass <= 8; pass++) {
            int changed = 0;
            for (Function f : fm.getFunctions(true)) {
                monitor.checkCancelled();
                // Unlock this function's own inputs so the decompiler reports what it reads.
                f.setSignatureSource(SourceType.DEFAULT);
                DecompileResults r = ifc.decompileFunction(f, 60, monitor);
                HighFunction hf = r.getHighFunction();
                List<Parameter> ps = new ArrayList<>();
                if (hf != null) {
                    LocalSymbolMap lsm = hf.getLocalSymbolMap();
                    for (int i = 0; i < lsm.getNumParams(); i++) {
                        HighSymbol hs = lsm.getParamSymbol(i);
                        if (hs.getStorage().isStackStorage() || hs.getStorage().isRegisterStorage())
                            ps.add(new ParameterImpl("param_" + (ps.size() + 1), hs.getDataType(),
                                                     hs.getStorage(), currentProgram));
                    }
                }
                String k = key(ps);
                setParams(f, ps);
                if (!k.equals(sig.get(f))) { sig.put(f, k); changed++; }
            }
            println("pass " + pass + ": " + changed + " signatures changed");
            if (changed == 0) break;
        }
        ifc.dispose();
    }
}
