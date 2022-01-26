"""
This profile create nodes connected to each other via a dedicated switch
and to auxillary nodes via a second LAN.
"""

import geni.portal as portal
import geni.rspec.pg as pg
import geni.rspec.emulab as emulab
import geni.urn as urn
import geni.aggregate.cloudlab as cloudlab

# Create a portal context.
pc = portal.Context()

# Create a Request object to start building the RSpec.
request = pc.makeRequestRSpec()

pc.defineParameter(
    "auxNodeCountPairs",
    "Auxillary node types and counts",
    portal.ParameterType.STRING,
    "c6525-25g:2",
    longDescription="Only connected to auxillary, not main network"
)

pc.defineParameter(
    "nodeCountPairs",
    "Physical node types and counts",
    portal.ParameterType.STRING,
    "m510:2,xl170:11,d6515:2",
    longDescription="PhysNodeType1:Count1,PhysNodeType2:Count2,...\nConnected to both networks.")

pc.defineParameter("nodeComponentIds",
                   "Physical node component ids (optional)",
                   portal.ParameterType.STRING,
                   "",
                   longDescription="NodeName1,NodeName2,...\nDon't include aux nodes")

pc.defineParameter(
    "switchType",
    "Switch type",
    portal.ParameterType.STRING,
    "dell-s4048", [('mlnx-sn2410', 'Mellanox SN2410'),
                   ('dell-s4048', 'Dell S4048'),
                   ('none', 'Just use a normal LAN')],
    longDescription=
    "Specify a physical switch type for the main network (dell-s4048,mlnx-sn2410,etc) ")

# Retrieve the values the user specifies during instantiation.
params = pc.bindParameters()

def parseNodeCounts(nodeCountPairsCSV, paramName):
    numNodes = 0
    nodeCounts = {}
    for nc in nodeCountPairsCSV.strip().split(","):
        fields = nc.split(":")
        if len(fields) != 2:
            pc.reportError(
                portal.ParameterError("invalid node count pair \"{0}\"".format(nc),
                                    [paramName]))
            continue
        nodeCounts[fields[0]] = int(fields[1])
        numNodes += nodeCounts[fields[0]]
    return numNodes, nodeCounts

auxNumNodes, auxNodeCounts = parseNodeCounts(params.auxNodeCountPairs, "auxNodeCountPairs")
numNodes, nodeCounts = parseNodeCounts(params.nodeCountPairs, "nodeCountPairs")

if numNodes < 1:
    pc.reportError(
        portal.ParameterError("must specify node types and counts",
                              ["nodeCountPairs"]))

if auxNumNodes < 0:
    pc.reportError(
        portal.ParameterError("must specify node types and counts",
                              ["auxNodeCountPairs"]))

componentIds = params.nodeComponentIds.split(",")
if not params.nodeComponentIds:
    componentIds = []

if len(componentIds) > 0:
    if len(nodeCounts) != 1:
        pc.reportError(
            portal.ParameterError(
                "can only use one node type is component ids are specified",
                ["nodeCountPairs", "nodeComponentIds"]))
    if len(componentIds) != numNodes:
        pc.reportError(
            portal.ParameterError(
                "found component ids specified for {0}/{1} (either set none or all)"
                .format(len(componentIds),
                        numNodes), ["nodeCountPairs", "nodeComponentIds"]))

# Create machines

ifaces = []
auxnetIfaces = []

counter = 1


def createNodes(nodeType, count, componentIds, numNodes, auxOnly=False):
    global counter
    global ifaces
    global auxnetIfaces
    for i in range(count):
        node = request.RawPC("node-" + str(counter))
        node.disk_image = "urn:publicid:IDN+emulab.net+image+emulab-ops//UBUNTU20-64-STD"
        node.hardware_type = nodeType
        if len(componentIds) == numNodes:
            node.component_id = urn.Node(cloudlab.Utah,
                                         componentIds[counter - 1])

        if not auxOnly:
            # Create iface and assign IP
            iface = node.addInterface("eth1")
            # Specify the IPv4 address
            iface.addAddress(
                pg.IPv4Address("192.168.2." + str(counter), "255.255.255.0"))
            ifaces.append((iface, "iface-node-" + str(counter)))

        # Create auxnet iface and assign IP
        iface = node.addInterface("eth2")
        # Specify the IPv4 address
        iface.addAddress(
            pg.IPv4Address("192.168.1." + str(counter), "255.255.255.0"))
        auxnetIfaces.append((iface, "auxnet-iface-node-" + str(counter)))

        counter += 1

for t, c in auxNodeCounts.items():
    createNodes(t, c, [], auxNumNodes, auxOnly=True)

for t, c in nodeCounts.items():
    createNodes(t, c, componentIds, numNodes, auxOnly=False)

auxlan = request.LAN("auxnet")

if params.switchType == "none":
    lan = request.LAN("lan")
    for iface, ifaceName in ifaces:
        lan.addInterface(iface)
else:
    # Create and connect switch
    s3switch = request.Switch("Switch")
    s3switch.hardware_type = params.switchType
    for iface, ifaceName in ifaces:
        siface = s3switch.addInterface()
        siface.addAddress(pg.IPv4Address("192.168.2.254", "255.255.255.0"))

        link = request.L1Link("link-" + ifaceName)
        link.addInterface(iface)
        link.addInterface(siface)

for iface, ifaceName in auxnetIfaces:
    auxlan.addInterface(iface)

# Print the RSpec to the enclosing page.
pc.printRequestRSpec(request)
