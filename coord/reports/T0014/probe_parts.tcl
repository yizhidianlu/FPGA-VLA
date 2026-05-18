puts ">>> probe_parts.tcl start"
set parts {
    xcvc1902-vsva2197-2MP-e-S
    xczu9eg-ffvb1156-2-e
    xczu7ev-ffvc1156-2-e
}

open_project probe_proj -reset

foreach p $parts {
    set sn "sol_[string map {- _ . _} $p]"
    puts ">>> trying part: $p (solution=$sn)"
    if {[catch {open_solution -reset $sn} oserr]} {
        puts "PROBE_SOLUTION_FAIL: $p :: $oserr"
        continue
    }
    if {[catch {set_part $p} sperr]} {
        puts "PROBE_PART_UNSUPPORTED: $p :: $sperr"
    } else {
        if {[catch {puts "  set_part OK, queried family: [get_part]"} qerr]} {
            puts "PROBE_PART_SUPPORTED: $p (get_part query failed: $qerr)"
        } else {
            puts "PROBE_PART_SUPPORTED: $p"
        }
    }
    close_solution
}

close_project
puts ">>> probe_parts.tcl done"
exit 0
