# Initialize registers
add $r3, $zero, $imm, 1    # Core ID = 1
add $r6, $zero, $imm, 128 # Loop bound
add $r2, $zero, $imm, 0    # Loop counter = 0

start:
    lw $r4, $zero, $zero, 0     # Check turn
    mul $R7, $R2, $imm, 4
    sub $R5, $R4, $R7, 0
    bne $imm, $r3, $r5, start  # Wait if not turn
    add $r4, $r4, $imm, 1    # Increment
    add $r2, $r2, $imm, 1    # Increment loop
    sw $r4, $zero, $zero ,0    # Store counter
    blt $imm, $r2, $r6, start # Continue if not done
    add $R9, $R9, $zero, 0
    halt $zero, $zero, $zero, 0