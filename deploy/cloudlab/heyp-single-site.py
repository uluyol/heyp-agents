"""This profile create a LAN connected via a dedicated switch."""

# Import the Portal object.
import geni.portal as portal
# Import the ProtoGENI library.
import geni.rspec.pg as pg
# Import the Emulab specific extensions.
import geni.rspec.emulab as emulab

# Create a portal context.
pc = portal.Context()

# Create a Request object to start building the RSpec.
request = pc.makeRequestRSpec()

# Variable number of nodes at two sites.
pc.defineParameter("nodeCountPairs", "Physical node types and counts",
                   portal.ParameterType.STRING,
                   "m510:2,xl170:11,d6515:2",
                   longDescription="PhysNodeType1:Count1,PhysNodeType2:Count2,...")


pc.defineParameter("switchType",  "Physical switch type",
                   portal.ParameterType.STRING, "dell-s4048",
                   [('mlnx-sn2410', 'Mellanox SN2410'), ('dell-s4048',  'Dell S4048')],
                   longDescription="Specify a physical switch type (dell-s4048,mlnx-sn2410,etc) ")

# Retrieve the values the user specifies during instantiation.
params = pc.bindParameters()

numNodes = 0
nodeCounts = {}
for nc in params.nodeCountPairs.strip().split(","):
    fields = nc.split(":")
    if len(fields) != 2:
        pc.reportError(portal.ParameterError("invalid node count pair \"{0}\"".format(nc)))
        continue
    nodeCounts[fields[0]] = int(fields[1])
    numNodes += nodeCounts[fields[0]]

if numNodes < 1:
    pc.reportError(portal.ParameterError("must specify node types and counts"))

# Create machines

ifaces = []

counter = 0
def createNodes(nodeType, count):
    global counter
    global ifaces
    for i in range(count):
        node = request.RawPC("node-" + str(counter))
        node.disk_image = "urn:publicid:IDN+emulab.net+image+emulab-ops//UBUNTU20-64-STD"
        node.hardware_type = nodeType
        # Create iface and assign IP
        iface = node.addInterface("eth1")
        # Specify the IPv4 address
        iface.addAddress(pg.IPv4Address("192.168.1." + str(counter + 1), "255.255.255.0"))
        ifaces.append((iface, "iface-node-" + str(counter)))
        counter += 1

for t, c in nodeCounts.items():
    createNodes(t, c)

# Create and connect switch
s3switch = request.Switch("Switch");
s3switch.hardware_type = params.switchType
for iface, ifaceName in ifaces:
    siface = s3switch.addInterface()
    siface.addAddress(pg.IPv4Address("192.168.1.254", "255.255.255.0"))

    link = request.L1Link("link-" + ifaceName)
    link.addInterface(iface)
    link.addInterface(siface)

# Print the RSpec to the enclosing page.
pc.printRequestRSpec(request)
