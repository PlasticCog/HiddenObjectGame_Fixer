// Decompiles the functions containing the given addresses (and optionally lists callers).
// Usage (headless): -postScript DecompileAt.java <outFile> <addr> [<addr> ...]
//   prefix an address with "callers:" to also decompile every caller of that function.
//   prefix with "xref:" to list every reference to that address (data or code).
// @category HOGFixer
import java.io.*;
import java.util.*;

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class DecompileAt extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File out = new File(args[0]);
        FunctionManager fm = currentProgram.getFunctionManager();
        DecompInterface di = new DecompInterface();
        di.toggleCCode(true);
        di.openProgram(currentProgram);
        try (PrintWriter pw = new PrintWriter(new FileWriter(out, true))) {
            for (int i = 1; i < args.length; i++) {
                String a = args[i];
                boolean callers = a.startsWith("callers:");
                boolean xref = a.startsWith("xref:");
                if (callers) a = a.substring(8);
                if (xref) a = a.substring(5);
                Address addr = toAddr(a);
                if (xref) {
                    pw.println("\n// ===== xrefs to " + addr);
                    for (Reference r : getReferencesTo(addr)) {
                        Function f = fm.getFunctionContaining(r.getFromAddress());
                        pw.println("//   " + r.getFromAddress() + " " + r.getReferenceType() + " in " +
                            (f != null ? f.getName() + "@" + f.getEntryPoint() : "?"));
                    }
                    continue;
                }
                Function f = fm.getFunctionContaining(addr);
                if (f == null) {
                    // try to create one
                    disassemble(addr);
                    f = createFunction(addr, null);
                }
                if (f == null) { pw.println("// no function at " + addr); continue; }
                List<Function> todo = new ArrayList<>();
                todo.add(f);
                if (callers) todo.addAll(f.getCallingFunctions(monitor));
                for (Function g : todo) {
                    pw.println("\n// ======================================================================");
                    StringBuilder cs = new StringBuilder();
                    for (Function c : g.getCallingFunctions(monitor)) cs.append(c.getEntryPoint()).append(' ');
                    pw.println("// " + g.getName() + " @ " + g.getEntryPoint() + "  callers: " + cs);
                    DecompileResults res = di.decompileFunction(g, 120, monitor);
                    pw.println(res != null && res.decompileCompleted()
                        ? res.getDecompiledFunction().getC()
                        : "// decompile failed");
                }
            }
        }
        di.dispose();
    }
}
