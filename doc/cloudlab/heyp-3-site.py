"""
DOES NOT WORK

This profile create lan that spans two clusters. Note that you must a bandwidth on your lan for this to work.

Instructions:
Click on any node in the topology and choose the `shell` menu item. When your shell window appears, use `ping` to test the link."""

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
pc.defineParameter("S1", "Number of Nodes at Site 1", portal.ParameterType.INTEGER, 2)
pc.defineParameter("S2", "Number of Nodes at Site 2", portal.ParameterType.INTEGER, 2)
pc.defineParameter("S3", "Number of Nodes at Site 3", portal.ParameterType.INTEGER, 2)

# Optional physical type for nodes.
pc.defineParameter("NodeT1",  "Optional physical node type for Site 1",
                   portal.ParameterType.STRING, "",
                   longDescription="Specify a physical node type (pc3000,d710,etc) " +
                   "instead of letting the resource mapper choose for you.")

pc.defineParameter("NodeT2",  "Optional physical node type for Site 2",
                   portal.ParameterType.STRING, "",
                   longDescription="Specify a physical node type (pc3000,d710,etc) " +
                   "instead of letting the resource mapper choose for you.")

pc.defineParameter("NodeT3",  "Optional physical node type for Site 3",
                   portal.ParameterType.STRING, "",
                   longDescription="Specify a physical node type (pc3000,d710,etc) " +
                   "instead of letting the resource mapper choose for you.")

pc.defineParameter("SwitchT",  "Physical switch type for Site 3",
                   portal.ParameterType.STRING, "dell-s4048",
                   [('mlnx-sn2410', 'Mellanox SN2410'), ('dell-s4048',  'Dell S4048')],
                   longDescription="Specify a physical switch type (dell-s4048,mlnx-sn2410,etc) ")

# Retrieve the values the user specifies during instantiation.
params = pc.bindParameters()

# Check parameter validity.
if params.S1 < 1:
    pc.reportError(portal.ParameterError("You must choose at least 1 node at Site 1"))
if params.S2 < 1:
    pc.reportError(portal.ParameterError("You must choose at least 1 node at Site 2"))
if params.S3 < 1:
    pc.reportError(portal.ParameterError("You must choose at least 1 node at Site 3"))

# Count for node name.
counter = 0

# ifaces. 
ifaces = {}

def populate_site(site_name, num_nodes_at_site, node_type):
    global counter
    global ifaces
    ifaces[site_name] = []
    for i in range(num_nodes_at_site):
        node = request.RawPC("node" + str(counter))
        node.Site(site_name)
        # Optional hardware type.
        if node_type != "":
            node.hardware_type = node_type
        # Create iface and assign IP
        iface = node.addInterface("eth1")
        # Specify the IPv4 address
        iface.addAddress(pg.IPv4Address("192.168.1." + str(counter + 1), "255.255.255.0"))
        ifaces[site_name].append((iface, "iface-node-" + str(counter)))
        counter += 1


populate_site("Site1", params.S1, params.NodeT1)
populate_site("Site2", params.S2, params.NodeT2)
populate_site("Site3", params.S3, params.NodeT3)

# Now add the link to the rspec. 
lan1 = request.LAN("lan1")
# Must provide a bandwidth. BW is in Kbps
lan1.bandwidth = 1000000 # 1 Gbps
# Add interfaces to lan
for iface, _ in ifaces["Site1"]:
    lan1.addInterface(iface)

# Now add the link to the rspec. 
lan2 = request.LAN("lan2")
# Must provide a bandwidth. BW is in Kbps
lan2.bandwidth = 1000000 # 1 Gbps
# Add interfaces to lan
for iface, _ in ifaces["Site2"]:
    lan2.addInterface(iface)

# Add Switch to the request and give it a couple of interfaces
s3switch = request.Switch("Site3Switch");
s3switch.Site("Site3")
s3switch.hardware_type = params.SwitchT

for iface, ifaceName in ifaces["Site3"]:
    link = request.L1Link("link-" + ifaceName)
    link.addInterface(iface)
    link.addInterface(s3switch.addInterface())

# Wire Sites 1 and 2 to Site 3
lan1.addInterface(s3switch.addInterface())
lan2.addInterface(s3switch.addInterface())

# Print the RSpec to the enclosing page.
pc.printRequestRSpec(request)
