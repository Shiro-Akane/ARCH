import pynucastro as pyna

# Select the nuclei in the aprox19 network.
aprox19_nuclei = [
    "h1", "he3", "he4", "c12", "n14", "o16", "ne20", "mg24", 
    "si28", "s32", "ar36", "ca40", "ti44", "cr48", "fe52", 
    "fe54", "ni56", "p", "n"
]

# Restrict ReacLib to reactions connecting the selected nuclei.
rl = pyna.ReacLibLibrary()
aprox19_lib = rl.linking_nuclei(aprox19_nuclei)

# Generate the C++ backend.
net = pyna.networks.SimpleCxxNetwork(libraries=[aprox19_lib])

# Export the generated network sources.
net.write_network("src/physics/network/aprox19")
