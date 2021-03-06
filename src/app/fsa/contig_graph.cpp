#include "contig_graph.hpp"

#include <cassert>
#include <fstream>
#include <iostream>
#include <stack>
#include <algorithm>
#include <random>
#include <unordered_set>


std::vector<Seq::Area> ContigEdge::GetSeqArea() {
    assert(link_->Best() != nullptr);
    return link_->Best()->GetSeqArea(std::abs(out_node_->id_)-1, out_node_->id_ > 0 ? 'B' : 'E');
}

ContigGraph::ContigGraph(ContigLinkStore &links) : links_(links) {
}

ContigGraph::~ContigGraph() {
    for (auto &n : nodes_) {
        delete n.second;
    }

    for (auto &e : edges_) {
        delete e.second;
    }
}

void ContigGraph::Create() {
    assert(contained_.size() == 0);
    for (auto& l0 : links_.Get()) {
        for (auto &l1 : l0.second) {
            if (l1.second.Valid()) {
                ContigLink::Loc loc = links_.Location(l1.second);
                if (loc ==  ContigLink::Loc::Containing) {
                    contained_.insert(l1.first);
                } else if (loc ==  ContigLink::Loc::Contained) {
                    contained_.insert(l0.first);
                } else if (loc == ContigLink::Loc::Equal) {
                    contained_.insert(std::max(l0.first, l1.first));
                }            
            }
        }
    }

    LOG(INFO)("Number of Contained Contigs: %zd", contained_.size());

    for (auto& l0 : links_.Get()) {
        for (auto &l1 : l0.second) {
            if (l1.second.Valid()) {
                if (contained_.find(l0.first) == contained_.end() && contained_.find(l1.first) == contained_.end()) {
                    
                    ContigLink::Loc loc = links_.Location(l1.second);
                    if (loc == ContigLink::Loc::Left || loc == ContigLink::Loc::Right) {
                        AddLink(l1.second);
                    }
                }
            }
        }
    }

}

void ContigGraph::AddLink(ContigLink& bunch) {
    assert(bunch.Best() != nullptr);
    assert(bunch.Best()->source.id < bunch.Best()->target.id);

    Seq::EndId sB = Seq::IdToEndId(bunch.Best()->source.id, 0);
    Seq::EndId sE = Seq::IdToEndId(bunch.Best()->source.id, 1);
    Seq::EndId tB = Seq::IdToEndId(bunch.Best()->target.id, 0);
    Seq::EndId tE = Seq::IdToEndId(bunch.Best()->target.id, 1);

    ContigLink::Loc loc = links_.Location(bunch);
    if (bunch.Best()->SameStrand()) {
        if (loc == ContigLink::Loc::Left) {
            AddEdge(tB, sB, bunch);
            AddEdge(sE, tE, bunch);
        }
        else {
            assert(loc == ContigLink::Loc::Right);
            AddEdge(sB, tB, bunch);
            AddEdge(tE, sE, bunch);
        }
    }
    else {
        if (loc == ContigLink::Loc::Left) {
            AddEdge(tB, sE, bunch);
            AddEdge(sB, tE, bunch);
        }
        else {
            assert(loc == ContigLink::Loc::Right);
            AddEdge(sE, tB, bunch);
            AddEdge(tE, sB, bunch);
        }
    }
}

void ContigGraph::AddEdge(Seq::EndId in_id, Seq::EndId out_id, ContigLink& link) {
    assert(in_id != out_id);

    auto in = nodes_.find(in_id);
    if (in == nodes_.end()) {
        auto r = nodes_.insert(std::make_pair(in_id, new ContigNode(in_id)));

        assert(r.second);
        in = r.first;
    }

    auto out = nodes_.find(out_id);
    if (out == nodes_.end()) {
        auto r = nodes_.insert(std::make_pair(out_id, new ContigNode(out_id)));

        assert(r.second);
        out = r.first;
    }

    ContigEdge *e = new ContigEdge(in->second, out->second);
    e->link_ = &link;
    edges_[e->Id()] = e;

    in->second->out_edges_.push_back(e);
    out->second->in_edges_.push_back(e);
}


void ContigGraph::CheckRepeat() {
    LOG(INFO)("Check repeat contigs");
    auto comm_size = [] (const std::unordered_set<Seq::Id>& g0, const std::unordered_set<Seq::Id> &g1) {
        size_t count = 0;
        for (auto i0: g0) {
            if (g1.find(i0) != g1.end()) count ++;
        }
        return count;
    };

    for (auto &it : nodes_) {
        auto &n = it.second;
        if (n->InDegree() > 1 && n->OutDegree() > 1) {
            std::vector<std::unordered_set<Seq::Id>> in_sup(n->InDegree());
            std::vector<std::unordered_set<Seq::Id>> out_sup(n->OutDegree());

            for (size_t i=0; i < n->in_edges_.size(); ++i) {
                for (auto li : *n->in_edges_[i]->link_->best_group) {
                    in_sup[i].insert(li->ReadId());                    
                }
            }

            for (size_t i=0; i < n->out_edges_.size(); ++i) {
                for (auto li : *n->out_edges_[i]->link_->best_group) {
                    out_sup[i].insert(li->ReadId());                    
                }
            }

            std::vector<size_t> score(n->InDegree() * n->OutDegree());

            for (size_t i_in=0; i_in < n->in_edges_.size(); ++i_in) {
                for (size_t i_out = 0; i_out < n->out_edges_.size(); ++i_out) {
                    score[i_in*n->OutDegree()+i_out] = comm_size(in_sup[i_in], out_sup[i_out]);
                }
            }

        }

    }
    
}

void ContigGraph::IdentifyPaths(const std::string &method) {

    CheckRepeat();

    std::unordered_set<ContigNode*> visited;

    for (auto &i : nodes_) {
        ContigNode* n = i.second;

        if (visited.find(n) == visited.end())
            paths_.push_back(ExtendPath(n, visited, method));
    }
}


template<typename I, typename O>
std::deque<ContigNode*> ContigGraph::ExtendPathWithMethod(ContigNode* n, std::unordered_set<ContigNode*> &visited, I get_in_node, O get_out_node) {
    assert(visited.find(n) == visited.end());

    std::deque<ContigNode*> path;

    visited.insert(n);
    visited.insert(ReverseNode(n));

    path.push_back(n);

    ContigNode* next = get_out_node(path.back(), visited);
    while (next != nullptr) {
        assert(visited.find(next) == visited.end());

        path.push_back(next);

        visited.insert(next);
        visited.insert(ReverseNode(next));

        next = get_out_node(next, visited);
    }

    ContigNode* prev = get_in_node(path.front(), visited);
    while (prev != nullptr) {
        assert(visited.find(prev) == visited.end());

        path.push_front(prev);

        visited.insert(prev);
        visited.insert(ReverseNode(prev));

        prev = get_in_node(prev, visited);
    }

    return path;
}

std::deque<ContigNode*> ContigGraph::ExtendPath(ContigNode* n, std::unordered_set<ContigNode*> &visited, const std::string &method) {

    if (method == "no") {
        auto get_in_edge = [](const ContigNode *n, const std::unordered_set<ContigNode*> &visited) {
            if (n->InDegree() == 1 && n->in_edges_[0]->in_node_->OutDegree() == 1) {
                if (visited.find(n->in_edges_[0]->in_node_) == visited.end()) {
                    return n->in_edges_[0]->in_node_;
                }
            }
            return (ContigNode*)nullptr;
        };

        auto get_out_edge = [](const ContigNode *n, const std::unordered_set<ContigNode*> &visited) {
            if (n->OutDegree() == 1 && n->out_edges_[0]->out_node_->InDegree() == 1) {
                if (visited.find(n->out_edges_[0]->out_node_) == visited.end()) {
                    return n->out_edges_[0]->out_node_;
                }
            }
            return (ContigNode*)nullptr;
        };

        return ExtendPathWithMethod(n, visited, get_in_edge, get_out_edge);
    }
    else if (method == "random") {
        auto get_in_edge = [&](const ContigNode *n, const std::unordered_set<ContigNode*> &visited) {
            std::random_device rd;
            std::vector<ContigNode*> cand;

            for (auto e : n->in_edges_) {
                if (visited.find(e->in_node_) == visited.end()) {
                    cand.push_back(e->in_node_);
                }
            }
            if (cand.size() > 0) return cand[rd() % cand.size()];

            return (ContigNode*)nullptr;
        };

        auto get_out_edge = [&](const ContigNode *n, const std::unordered_set<ContigNode*> &visited) {
            std::random_device rd;
            std::vector<ContigNode*> cand;

            for (auto e : n->out_edges_) {
                if (visited.find(e->out_node_) == visited.end()) {
                    cand.push_back(e->out_node_);
                }
            }
            if (cand.size() > 0) return cand[rd() % cand.size()];

            return (ContigNode*)nullptr;
        };


        return ExtendPathWithMethod(n, visited, get_in_edge, get_out_edge);
    }
    else if (method == "best") {
        auto get_in_edge = [&](const ContigNode *n, const std::unordered_set<ContigNode*> &visited) {
            
            if (n->InDegree() > 0) {
                assert(n->best_in_edge_ != nullptr);
                if (n->best_in_edge_ == n->best_in_edge_->in_node_->best_out_edge_ && 
                    visited.find(n->best_in_edge_->in_node_) == visited.end())
                    return n->best_in_edge_->in_node_;
            }
            return (ContigNode*)nullptr;
        };

        auto get_out_edge = [&](const ContigNode *n, const std::unordered_set<ContigNode*> &visited) {
            
            if (n->OutDegree() > 0) {
                assert(n->best_out_edge_ != nullptr);
                if (n->best_out_edge_ == n->best_out_edge_->out_node_->best_in_edge_ &&
                    visited.find(n->best_out_edge_->out_node_) == visited.end())
                    return n->best_out_edge_->out_node_;
            }

            return (ContigNode*)nullptr;
        };


        return ExtendPathWithMethod(n, visited, get_in_edge, get_out_edge);
    }
    else {
        LOG(FATAL)("%s is not a valid option of the parameter select_branch.", method.c_str());
        return std::deque<ContigNode*>{n};
    }
}

void ContigGraph::Output(const std::string &fname) {
    std::ofstream of(fname);

    if (of.is_open()) {
        for (auto e : edges_) {
            of << e.second->in_node_->id_ << ", " << e.second->out_node_->id_ << "\n";
        }
    }
}



void ContigGraph::CalucateBest(const std::string &method) {
    if (method == "support") {
        for (auto & i : nodes_) {
            ContigNode *n = i.second;

            if (n->out_edges_.size() > 0) {
                n->best_out_edge_ = n->out_edges_[0];

                for (size_t i = 1; i < n->out_edges_.size(); ++i) {
                    if (n->best_out_edge_->Support() < n->out_edges_[i]->Support()) {
                        n->best_out_edge_ = n->out_edges_[i];
                    }
                }
            }

            if (n->in_edges_.size() > 0) {
                n->best_in_edge_ = n->in_edges_[0];

                for (size_t i = 1; i < n->in_edges_.size(); ++i) {
                    if (n->best_in_edge_->Support() < n->in_edges_[i]->Support()) {
                        n->best_in_edge_ = n->in_edges_[i];
                    }
                }
            }
        }

    } else {
        for (auto & i : nodes_) {
            ContigNode *n = i.second;

            if (n->out_edges_.size() > 0) {
                n->best_out_edge_ = n->out_edges_[0];

                for (size_t i = 1; i < n->out_edges_.size(); ++i) {
                    if (n->best_out_edge_->LinkLength() < n->out_edges_[i]->LinkLength()) {
                        n->best_out_edge_ = n->out_edges_[i];
                    }
                }
            }

            if (n->in_edges_.size() > 0) {
                n->best_in_edge_ = n->in_edges_[0];

                for (size_t i = 1; i < n->in_edges_.size(); ++i) {
                    if (n->best_in_edge_->LinkLength() < n->in_edges_[i]->LinkLength()) {
                        n->best_in_edge_ = n->in_edges_[i];
                    }
                }
            }
        }
    }
}
