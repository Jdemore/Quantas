//
//  BlockGuardSharded.cpp
//  BlockGuard
//
//  Created by Kendric Hood on 3/28/19.
//  Copyright © 2019 Kent State University. All rights reserved.
//
//
#include "PBFTPeer_Sharded.hpp"

PBFTPeer_Sharded::PBFTPeer_Sharded(std::string id) : PBFT_Peer(id) {
    _groupId = -1;
    _committeeId = -1;
    _groupMembers = std::map<std::string, Peer<PBFT_Message>* >();
    _committeeMembers = std::map<std::string, Peer<PBFT_Message>* >();
    _printComitte = false;
    _printGroup = false;
}

PBFTPeer_Sharded::PBFTPeer_Sharded(const PBFTPeer_Sharded &rhs) : PBFT_Peer(rhs){
    _groupId = rhs._groupId;
    _committeeId = rhs._committeeId;
    _groupMembers = rhs._groupMembers;
    _committeeMembers = rhs._committeeMembers;
    _printComitte = rhs._printComitte;
    _printGroup = rhs._printGroup;
}

void PBFTPeer_Sharded::braodcast(const PBFT_Message &msg){
    for (auto it=_committeeMembers.begin(); it!=_committeeMembers.end(); ++it){
        std::string neighborId = it->first;
        Packet<PBFT_Message> pck(makePckId());
        pck.setSource(_id);
        pck.setTarget(neighborId);
        pck.setBody(msg);
        _outStream.push_back(pck);
    }
}

void PBFTPeer_Sharded::commitRequest(){
    PBFT_Message commit = _currentRequest;
    commit.result = _currentRequestResult;
    commit.round = _currentRound;    
    // count the number of byzantine commits 
    // >= 1/3 then we will commit a defeated transaction
    // < 1/3 and an honest primary will commit
    // < 1/3 and byzantine primary will view change
    int numberOfByzantineCommits = 0;
    for(auto commitMsg = _commitLog.begin(); commitMsg != _commitLog.end(); commitMsg++){
        if(commitMsg->sequenceNumber == _currentRequest.sequenceNumber
                && commitMsg->view == _currentView
                && commitMsg->result == _currentRequestResult){
                    if(commitMsg->byzantine){
                        numberOfByzantineCommits++; 
                    }
                }
    }
    if(numberOfByzantineCommits < faultyPeers() && !_currentRequest.byzantine){
        commit.defeated = false;
        _ledger.push_back(commit);
    }else if(numberOfByzantineCommits >= faultyPeers() && _currentRequest.byzantine){
        commit.defeated = true;
        _ledger.push_back(commit);
    }else{ 
        viewChange(_committeeMembers); 
    }
    _currentPhase = IDEAL; // complete distributed-consensus
    _currentRequestResult = 0;
    _currentRequest = PBFT_Message();
    cleanLogs();
}

std::vector<std::string> PBFTPeer_Sharded::getGroupMembers()const{
    std::vector<std::string> groupIds = std::vector<std::string>();
    for (auto it=_groupMembers.begin(); it!=_groupMembers.end(); ++it){
        groupIds.push_back(it->first);
    }
    return groupIds;
};

std::vector<std::string> PBFTPeer_Sharded::getCommitteeMembers()const{
    std::vector<std::string> committeeIds = std::vector<std::string>();
    for (auto it=_committeeMembers.begin(); it!=_committeeMembers.end(); ++it){
        committeeIds.push_back(it->first);
    }
    return committeeIds;
};

void PBFTPeer_Sharded::preformComputation(){
    if(_primary == nullptr){
        _primary = findPrimary(_committeeMembers);
    }
    int oldLedgerSize = _ledger.size();
    collectMessages(); // sorts messages into there repective logs
    prePrepare();
    prepare();
    waitPrepare();
    commit();
    waitCommit();
    // if ledger has grown in size then this peer has comited and is free from committee 
    bool hasComited = _ledger.size() > oldLedgerSize;
    if(hasComited){
        clearCommittee();
    }
    _currentRound++;
}

PBFTPeer_Sharded& PBFTPeer_Sharded::operator= (const PBFTPeer_Sharded &rhs){
    PBFT_Peer::operator=(rhs);
    
    _groupId = rhs._groupId;
    _committeeId = rhs._committeeId;
    _groupMembers = rhs._groupMembers;
    _committeeMembers = rhs._committeeMembers;
    _printComitte = rhs._printComitte;
    _printGroup = rhs._printGroup;
    
    return *this;
}

std::ostream& PBFTPeer_Sharded::printTo(std::ostream &out)const{
    PBFT_Peer::printTo(out);
    
    out<< "\t"<< std::setw(LOG_WIDTH)<< "Group Id"<< std::setw(LOG_WIDTH)<< "Committee Id"<< std::setw(LOG_WIDTH)<< "Group Size"<< std::setw(LOG_WIDTH)<< "Committee Size"<<  std::endl;
    out<< "\t"<< std::setw(LOG_WIDTH)<< _groupId<< std::setw(LOG_WIDTH)<< _committeeId<< std::setw(LOG_WIDTH)<< _groupMembers.size() + 1<< std::setw(LOG_WIDTH)<< _committeeMembers.size() + 1<< std::endl<< std::endl;
    
    std::vector<std::string> groupMembersIds = std::vector<std::string>();
    for (auto it=_groupMembers.begin(); it!=_groupMembers.end(); ++it){
        groupMembersIds.push_back(it->first);
    }
    
    std::vector<std::string> committeeMembersIds = std::vector<std::string>();
    for (auto it=_committeeMembers.begin(); it!=_committeeMembers.end(); ++it){
        committeeMembersIds.push_back(it->first);
    }
    
    while(committeeMembersIds.size() > groupMembersIds.size()){
        groupMembersIds.push_back("");
    }
    while(groupMembersIds.size() > committeeMembersIds.size()){
        committeeMembersIds.push_back("");
    }

    if(_printComitte || _printGroup){
        out<< "\t"<< std::setw(LOG_WIDTH)<< "Group Members"<< std::setw(LOG_WIDTH)<< "Committee Members"<< std::endl;
    }
    for(int i = 0; i < committeeMembersIds.size(); i++){
        if(_printComitte && _printGroup){
            out<< "\t"<< std::setw(LOG_WIDTH)<< groupMembersIds[i]<< std::setw(LOG_WIDTH)<< committeeMembersIds[i]<< std::endl;
        }else if(_printComitte){
            out<< "\t"<< std::setw(LOG_WIDTH)<< ""<< std::setw(LOG_WIDTH)<< committeeMembersIds[i]<< std::endl;
        }else if(_printGroup){
            out<< "\t"<< std::setw(LOG_WIDTH)<< groupMembersIds[i]<< std::setw(LOG_WIDTH)<< ""<< std::endl;
        }
    }
    
    return out;
}
