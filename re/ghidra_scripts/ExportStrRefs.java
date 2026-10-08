// Export every string in DATA32 with the functions that reference it.
// Script args: <out tsv>
// @category TerraFire
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.Reference;
import java.io.*;
import java.util.*;

public class ExportStrRefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        try (PrintWriter w = new PrintWriter(new FileWriter(getScriptArgs()[0]))) {
            w.println("str_addr\tfuncs\tstring");
            for (Data d : currentProgram.getListing().getDefinedData(true)) {
                if (!d.hasStringValue()) continue;
                Set<String> fs = new TreeSet<>();
                for (Reference r : getReferencesTo(d.getAddress())) {
                    Function f = getFunctionContaining(r.getFromAddress());
                    fs.add(f == null ? r.getFromAddress().toString() : f.getEntryPoint().toString());
                }
                String s = StringDataInstance.getStringDataInstance(d).getStringValue();
                if (s == null) continue;
                w.printf("%s\t%s\t%s%n", d.getAddress(), String.join(",", fs),
                         s.replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r"));
            }
        }
    }
}
