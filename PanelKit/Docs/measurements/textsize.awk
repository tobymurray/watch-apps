# Sum .text* (excluding .ARM.exidx, .llvmbc) for archive members matching CRATE.
$0 ~ /\(ex .*\.a\):$/ { inmember = ($0 ~ crate) ; next }
inmember && /^\.text/ { t += $2 }
inmember && /^\.rodata/ { r += $2 }
END { printf "%d\t%d\n", t, r }
