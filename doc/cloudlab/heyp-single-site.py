"""This profile create a LAN connected via a dedicated switch."""

import geni.portal as portal
import geni.rspec.pg as pg
import geni.rspec.emulab as emulab
import geni.urn as urn
import geni.aggregate.cloudlab as cloudlab

# Create a portal context.
pc = portal.Context()

# Create a Request object to start building the RSpec.
request = pc.makeRequestRSpec()

# Variable number of nodes at two sites.
pc.defineParameter(
    "nodeCountPairs",
    "Physical node types and counts",
    portal.ParameterType.STRING,
    "m510:2,xl170:11,d6515:2",
    longDescription="PhysNodeType1:Count1,PhysNodeType2:Count2,...")

# Variable number of nodes at two sites.
pc.defineParameter("nodeComponentIds",
                   "Physical node component ids (optional)",
                   portal.ParameterType.STRING,
                   "",
                   longDescription="NodeName1,NodeName2,...")

pc.defineParameter(
    "switchType",
    "Switch type",
    portal.ParameterType.STRING,
    "dell-s4048", [('mlnx-sn2410', 'Mellanox SN2410'),
                   ('dell-s4048', 'Dell S4048'),
                   ('none', 'Just use a normal LAN')],
    longDescription=
    "Specify a physical switch type (dell-s4048,mlnx-sn2410,etc) ")

# Retrieve the values the user specifies during instantiation.
params = pc.bindParameters()

numNodes = 0
nodeCounts = {}
for nc in params.nodeCountPairs.strip().split(","):
    fields = nc.split(":")
    if len(fields) != 2:
        pc.reportError(
            portal.ParameterError("invalid node count pair \"{0}\"".format(nc),
                                  ["nodeCountPairs"]))
        continue
    nodeCounts[fields[0]] = int(fields[1])
    numNodes += nodeCounts[fields[0]]

if numNodes < 1:
    pc.reportError(
        portal.ParameterError("must specify node types and counts",
                              ["nodeCountPairs"]))

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

counter = 1


def createNodes(nodeType, count):
    global counter
    global ifaces
    for i in range(count):
        node = request.RawPC("node-" + str(counter))
        node.disk_image = "urn:publicid:IDN+emulab.net+image+emulab-ops//UBUNTU20-64-STD"
        node.hardware_type = nodeType
        if len(componentIds) == numNodes:
            node.component_id = urn.Node(cloudlab.Utah,
                                         componentIds[counter - 1])
        # Create iface and assign IP
        iface = node.addInterface("eth1")
        # Specify the IPv4 address
        iface.addAddress(
            pg.IPv4Address("192.168.1." + str(counter), "255.255.255.0"))
        ifaces.append((iface, "iface-node-" + str(counter)))
        counter += 1


for t, c in nodeCounts.items():
    createNodes(t, c)

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
        siface.addAddress(pg.IPv4Address("192.168.1.254", "255.255.255.0"))

        link = request.L1Link("link-" + ifaceName)
        link.addInterface(iface)
        link.addInterface(siface)

# Print the RSpec to the enclosing page.
pc.printRequestRSpec(request)
