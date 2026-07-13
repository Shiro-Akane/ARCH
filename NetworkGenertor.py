import pynucastro as pyna

# 1. 选定核素
aprox19_nuclei = [
    "h1", "he3", "he4", "c12", "n14", "o16", "ne20", "mg24", 
    "si28", "s32", "ar36", "ca40", "ti44", "cr48", "fe52", 
    "fe54", "ni56", "p", "n"
]

# 2. 链接库
rl = pyna.ReacLibLibrary()
aprox19_lib = rl.linking_nuclei(aprox19_nuclei)

# 3. 重点：CXX 后端
net = pyna.networks.SimpleCxxNetwork(libraries=[aprox19_lib])

# 4. 导出
net.write_network("src/physics/network/aprox19")