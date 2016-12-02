/*
Copyright 2016 Thomas Jammet
mathieu.poux[a]gmail.com
jammetthomas[a]gmail.com

This file is part of Librtmfp.

Librtmfp is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Librtmfp is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with Librtmfp.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "FlowManager.h"
#include "Mona/StopWatch.h"
#include <set>

#define COOKIE_SIZE	0x40
#define PEER_LIST_ADDRESS_TYPE	std::map<Mona::SocketAddress, RTMFP::AddressType>

namespace P2PEvents {
	struct OnPeerClose : Mona::Event<void(const std::string& peerId, Mona::UInt8 mask, bool full)> {}; // notify parent that the peer is closing (update the NetGroup push flags)
}

/**************************************************
P2PConnection represents a direct P2P connection 
with another peer
*/
class RTMFPConnection;
class NetGroup;
class FlashListener;
struct RTMFPGroupConfig;
class P2PConnection : public FlowManager,
	public FlashEvents::OnGroupMedia,
	public FlashEvents::OnGroupReport,
	public FlashEvents::OnGroupPlayPush,
	public FlashEvents::OnGroupPlayPull,
	public FlashEvents::OnFragmentsMap,
	public FlashEvents::OnGroupBegin,
	public FlashEvents::OnFragment,
	public P2PEvents::OnPeerClose {
	friend class RTMFPConnection;
public:
	P2PConnection(RTMFPConnection* parent, std::string id, Invoker* invoker, OnSocketError pOnSocketError, OnStatusEvent pOnStatusEvent, OnMediaEvent pOnMediaEvent, const PEER_LIST_ADDRESS_TYPE& addresses,
		const Mona::SocketAddress& host, bool responder, bool group);

	virtual ~P2PConnection();

	virtual Mona::UDPSocket&	socket();

	// Add a command to the main stream (play/publish/netgroup)
	virtual void addCommand(CommandType command, const char* streamName, bool audioReliable = false, bool videoReliable = false);

	// Set the tag used for this connection (responder mode)
	void setTag(const std::string& tag) { _tag = tag; }

	// Call a function on the peer side
	// return 0 if it fails, 1 otherwise
	unsigned int callFunction(const char* function, int nbArgs, const char** args);

	// Return the tag used for this p2p connection (initiator mode)
	const std::string&	tag() { return _tag; }

	const Mona::SocketAddress& peerAddress() { return _targetAddress; }
	const Mona::SocketAddress& hostAddress() { return _hostAddress; }

	// Manage all handshake messages (marker 0x0B)
	virtual void manageHandshake(Mona::Exception& ex, Mona::BinaryReader& reader);

	// Handle the first P2P responder handshake message (called by RTMFPConnection)
	void responderHandshake0(Mona::Exception& ex, std::string tag, const Mona::SocketAddress& address);

	// Handle the second P2P responder handshake message
	void responderHandshake1(Mona::Exception& ex, Mona::BinaryReader& reader);

	// Send the second P2P initiator handshake message in a middle mode (local)
	void initiatorHandshake70(Mona::Exception& ex, Mona::BinaryReader& reader, const Mona::SocketAddress& address);

	// Send the third P2P initiator handshake message
	bool initiatorHandshake2(Mona::Exception& ex, Mona::BinaryReader& reader);

	// Flush the connection
	// marker values can be :
	// - 0B for handshake
	// - 0A for raw response in P2P mode (only for responder)
	// - 8A for AMF responde in P2P mode (only for responder)
	// - 4A for acknowlegment in P2P mode (TODO: see if it is needed)
	virtual void				flush(bool echoTime, Mona::UInt8 marker);

	virtual void				initWriter(const std::shared_ptr<RTMFPWriter>& pWriter);

	// Create a flow for special signatures (NetGroup)
	virtual RTMFPFlow*			createSpecialFlow(Mona::Exception& ex, Mona::UInt64 id, const std::string& signature);

	// Close the connection properly
	virtual void				close(bool full);

	virtual const std::string&	name() { return (peerId.empty()) ? _targetAddress.toString() : peerId; }

	// Update the host address of the peer
	void updateHostAddress(const Mona::SocketAddress& address);

	// Set the output address for next sending
	void setOutAddress(const Mona::SocketAddress& address);

	/*** NetGroup functions ***/

	// Update the Group Fragments map
	void updateFragmentsMap(Mona::UInt64 id, const Mona::UInt8* data, Mona::UInt32 size);

	// Return True if bit number is available in the fragments map (for push out mode)
	bool checkMask(Mona::UInt8 bitNumber);

	// Return True if the fragment is available
	bool hasFragment(Mona::UInt64 index);

	// Write the Group publication infos
	void sendGroupMedia(const std::string& stream, const Mona::UInt8* data, Mona::UInt32 size, RTMFPGroupConfig* groupConfig);

	// Send the group report (message 0A)
	void sendGroupReport(const Mona::UInt8* data, Mona::UInt32 size);

	// Create the flow if necessary and send media
	// The fragment is sent if pull is true or if this is a pushable fragment
	bool sendMedia(const Mona::UInt8* data, Mona::UInt32 size, Mona::UInt64 fragment, bool pull=false);

	// Send the Fragments map message
	// param lastFragment : latest fragment in the message
	void sendFragmentsMap(Mona::UInt64 lastFragment, const Mona::UInt8* data, Mona::UInt32 size);

	// Set the Group Publish Push mode (after a message 23)
	void setPushMode(Mona::UInt8 mode);

	// Update the Group Play Push mode
	void sendPushMode(Mona::UInt8 mode);

	// Send the group begin message (02 + 0E messages)
	void sendGroupBegin();

	// Send a pull request (2B)
	void sendPull(Mona::UInt64 index);

	// Send the Group Peer Connect request
	void sendGroupPeerConnect();

	// Add a fragment to the blacklist of pull to avoid a new pull request for this peer
	void addPullBlacklist(Mona::UInt64 idFragment);

	// Read addresses and add peer to heard list if needed
	// return : True if at least an address has been read
	static bool	ReadAddresses(Mona::BinaryReader& reader, PEER_LIST_ADDRESS_TYPE& addresses, Mona::SocketAddress& hostAddress);

	/*** Public members ***/

	Mona::UInt8						attempt; // Number of try to contact the responder (only for initiator)
	Mona::Time						lastTry; // Last time handshake 30 has been sent to the server (only for initiator)

	std::string						rawId; // Peer Id in binary format + header (210f)
	std::string						peerId; // Peer Id of the peer connected
	static Mona::UInt32				P2PSessionCounter; // Global counter for generating incremental P2P sessions id

	// NetGroup members
	bool							mediaSubscriptionSent; // True if the media subscription has been sent
	bool							mediaSubscriptionReceived; // True if the media subscription message has been received
	bool							groupFirstReportSent; // True if the first group report has been sent
	Mona::UInt8						pushInMode; // Group Play Push mode
	bool							groupReportInitiator; // True if we are the initiator of last Group Report (to avoid endless exchanges)
	bool							isGroupDisconnected; // True if the group connection has been disconnected (group writer consumed)

protected:
	// Handle play request (only for P2PConnection)
	virtual bool					handlePlay(const std::string& streamName, FlashWriter& writer);

	// Handle a 0C Message
	virtual void					handleProtocolFailed();

	// Handle a Writer close message (type 5E)
	virtual void					handleWriterFailed(RTMFPWriter* pWriter);

	// Handle a P2P address exchange message (Only for RTMFPConnection)
	virtual void					handleP2PAddressExchange(Mona::Exception& ex, Mona::PacketReader& reader);

private:
	// Close the Group connection to peer
	void							closeGroup(bool full);

	// Return true if the new fragment is pushable (according to the Group push mode)
	bool							isPushable(Mona::UInt8 rest);

	// Handle a NetGroup connection message from a peer connected (only for P2PConnection)
	void							handleGroupHandshake(const std::string& groupId, const std::string& key, const std::string& id);

	RTMFPConnection*				_parent; // RTMFPConnection related to
	FlashListener*					_pListener; // Listener of the main publication (only one by intance)
	Mona::UInt32					_sessionId; // id of the P2P session;
	std::string						_farKey; // Public Key of the server/peer (for shared key determination)
	std::string						_farNonce; // Nonce of the distant peer

	Mona::SocketAddress				_hostAddress; // Address of the server related to this peer

	// Play/Publish command
	std::string						_streamName; // playing stream name
	bool							_responder; // is responder?

	bool							_rawResponse; // next message is a raw response? TODO: make it nicer

	// Group members
	std::shared_ptr<Mona::Buffer>	_groupConnectKey; // Encrypted key used to connect to the peer
	bool							_groupConnectSent; // True if group connection request has been sent to peer
	bool							_groupBeginSent; // True if the group messages 02 + 0E have been sent
	bool							_isGroup; // True if this peer connection it part of a NetGroup

	Mona::UInt8						_pushOutMode; // Group Publish Push mode

	Mona::UInt64					_idMediaReportFlow; // Id of the Media Report flow to assign it to the Media Writer
	std::shared_ptr<RTMFPWriter>	_pMediaWriter; // Writer for media packets
	std::shared_ptr<RTMFPWriter>	_pMediaReportWriter; // Writer for fragments Map messages and media related messages
	std::shared_ptr<RTMFPWriter>	_pReportWriter; // Writer for report messages
	std::shared_ptr<RTMFPWriter>	_pNetStreamWriter; // Writer for NetStream P2P direct messages

	Mona::Buffer					_fragmentsMap; // Last Fragments Map received
	Mona::UInt64					_idFragmentMap; // Last ID received from the Fragments Map
	Mona::UInt64					_lastIdSent; // Last ID sent in the Fragments map

	std::set<Mona::UInt64>			_setPullBlacklist; // set of fragments blacklisted for pull requests to this peer

	PEER_LIST_ADDRESS_TYPE			_knownAddresses; // list of known addresses of the peer

	FlashConnection::OnGroupHandshake::Type				onGroupHandshake; // Received when a connected peer send us the Group hansdhake (only for P2PConnection)
};
