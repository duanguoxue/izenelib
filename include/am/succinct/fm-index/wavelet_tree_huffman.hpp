#ifndef _FM_INDEX_WAVELET_TREE_HUFFMAN_HPP
#define _FM_INDEX_WAVELET_TREE_HUFFMAN_HPP

#include "wavelet_tree.hpp"
#include "wavelet_tree_node.hpp"

#include <queue>


NS_IZENELIB_AM_BEGIN

namespace succinct
{
namespace fm_index
{

template <class CharT>
class WaveletTreeHuffman : public WaveletTree<CharT>
{
public:
    typedef CharT char_type;
    typedef WaveletTreeHuffman<CharT> self_type;

    WaveletTreeHuffman(uint64_t alphabet_num, bool support_select, bool dense);
    ~WaveletTreeHuffman();

    void build(const char_type *char_seq, size_t len);

    char_type access(size_t pos) const;
    char_type access(size_t pos, size_t &rank) const;

    size_t rank(char_type c, size_t pos) const;
    size_t select(char_type c, size_t rank) const;

    void intersect(
            const std::vector<std::pair<size_t, size_t> > &ranges,
            size_t thres,
            size_t max_count,
            std::vector<char_type> &results) const;

    void topKUnion(
            const std::vector<boost::tuple<size_t, size_t, double> > &ranges,
            size_t topK,
            std::vector<std::pair<double, char_type> > &results) const;

    void topKUnionWithFilters(
            const std::vector<std::pair<size_t, size_t> > &filters,
            const std::vector<boost::tuple<size_t, size_t, double> > &ranges,
            size_t topK,
            std::vector<std::pair<double, char_type> > &results) const;

    void topKUnionWithAuxFilters(
            const std::vector<FilterList<self_type> *> &aux_filters,
            const std::vector<std::pair<size_t, size_t> > &filters,
            const std::vector<boost::tuple<size_t, size_t, double> > &ranges,
            size_t topK,
            std::vector<std::pair<double, char_type> > &results) const;

    size_t getOcc(char_type c) const;
    WaveletTreeNode *getRoot() const;

    size_t length() const;
    size_t allocSize() const;

    void save(std::ostream &ostr) const;
    void load(std::istream &istr);

private:
    void makeCodeMap_(uint64_t code, size_t level, WaveletTreeNode *node);

    void deleteTree_(WaveletTreeNode *node);
    void buildTreeNodes_(WaveletTreeNode *node);

    void recursiveIntersect_(
            const WaveletTreeNode *node,
            const std::vector<std::pair<size_t, size_t> > &ranges,
            size_t thres,
            size_t max_count,
            std::vector<char_type> &results) const;

    size_t getTreeSize_(const WaveletTreeNode *node) const;

    void saveTree_(std::ostream &ostr, const WaveletTreeNode *node) const;
    void loadTree_(std::istream &istr, WaveletTreeNode *node);

private:
    std::vector<size_t> occ_;
    std::vector<uint64_t> code_map_;
    WaveletTreeNode *root_;
    std::vector<WaveletTreeNode *> leaves_;
};

template <class CharT>
WaveletTreeHuffman<CharT>::WaveletTreeHuffman(uint64_t alphabet_num, bool support_select, bool dense)
    : WaveletTree<CharT>(alphabet_num, support_select, dense)
    , root_()
{
}

template <class CharT>
WaveletTreeHuffman<CharT>::~WaveletTreeHuffman()
{
    if (root_) deleteTree_(root_);
}

template <class CharT>
void WaveletTreeHuffman<CharT>::deleteTree_(WaveletTreeNode *node)
{
    if (node->left_) deleteTree_(node->left_);
    if (node->right_) deleteTree_(node->right_);
    delete node;
}

template <class CharT>
void WaveletTreeHuffman<CharT>::build(const char_type *char_seq, size_t len)
{
    if (this->alphabet_num_ == 0) return;

    this->alphabet_bit_num_ = bits(this->alphabet_num_ - 1);

    occ_.resize((1 << this->alphabet_bit_num_) + 1);
    for (size_t i = 0; i < len; ++i)
    {
        ++occ_[char_seq[i] + 1];
    }

    leaves_.resize(this->alphabet_num_);
    std::priority_queue<WaveletTreeNode *, std::vector<WaveletTreeNode *>, std::greater<WaveletTreeNode *> > node_queue;

    for (size_t i = 0; i < this->alphabet_num_; ++i)
    {
        if (occ_[i + 1])
        {
            leaves_[i] = new WaveletTreeNode(i, occ_[i + 1], this->support_select_, this->dense_);
            node_queue.push((leaves_[i]));
        }
    }

    while (node_queue.size() > 1)
    {
        WaveletTreeNode *left = node_queue.top();
        node_queue.pop();

        WaveletTreeNode *right = node_queue.top();
        node_queue.pop();

        node_queue.push(new WaveletTreeNode(left, right, this->support_select_, this->dense_));
    }

    root_ = node_queue.top();
    node_queue.pop();

    code_map_.resize(this->alphabet_num_);
    makeCodeMap_(0, 0, root_);

    for (size_t i = 0; i < this->alphabet_num_; ++i)
    {
        if (leaves_[i] && leaves_[i]->parent_)
        {
            if (leaves_[i]->parent_->left_ == leaves_[i])
            {
                leaves_[i]->parent_->c0_ = i;
                leaves_[i] = leaves_[i]->parent_;

                delete leaves_[i]->left_;
                leaves_[i]->left_ = NULL;
            }
            else
            {
                leaves_[i]->parent_->c1_ = i;
                leaves_[i] = leaves_[i]->parent_;

                delete leaves_[i]->right_;
                leaves_[i]->right_ = NULL;
            }
        }
    }

    for (size_t i = 2; i < occ_.size(); ++i)
    {
        occ_[i] += occ_[i - 1];
    }

    uint64_t code;
    size_t level;
    WaveletTreeNode *walk;

    for (size_t i = 0; i < len; ++i)
    {
        code = code_map_[char_seq[i]];
        walk = root_;

        for (level = 0; walk; ++level)
        {
            if (code & 1ULL << level)
            {
                walk->append1();
                walk = walk->right_;
            }
            else
            {
                walk->append0();
                walk = walk->left_;
            }
        }
    }

    buildTreeNodes_(root_);
}

template <class CharT>
void WaveletTreeHuffman<CharT>::makeCodeMap_(uint64_t code, size_t level, WaveletTreeNode *node)
{
    if (node->left_)
    {
        makeCodeMap_(code, level + 1, node->left_);
        makeCodeMap_(code | 1ULL << level, level + 1, node->right_);
    }
    else
    {
        code_map_[node->c0_] = code;
    }
}

template <class CharT>
void WaveletTreeHuffman<CharT>::buildTreeNodes_(WaveletTreeNode *node)
{
    if (!node) return;
    node->build();
    buildTreeNodes_(node->left_);
    buildTreeNodes_(node->right_);
}

template <class CharT>
CharT WaveletTreeHuffman<CharT>::access(size_t pos) const
{
    if (pos >= length()) return -1;

    WaveletTreeNode *walk = root_;

    while (true)
    {
        if (walk->access(pos, pos))
        {
            if (walk->right_) walk = walk->right_;
            else return walk->c1_;
        }
        else
        {
            if (walk->left_) walk = walk->left_;
            else return walk->c0_;
        }
    }
}

template <class CharT>
CharT WaveletTreeHuffman<CharT>::access(size_t pos, size_t &rank) const
{
    if (pos >= length()) return -1;

    WaveletTreeNode *walk = root_;

    while (true)
    {
        if (walk->access(pos, pos))
        {
            if (walk->right_)
            {
                walk = walk->right_;
            }
            else
            {
                rank = pos;
                return walk->c1_;
            }
        }
        else
        {
            if (walk->left_)
            {
                walk = walk->left_;
            }
            else
            {
                rank = pos;
                return walk->c0_;
            }
        }
    }
}

template <class CharT>
size_t WaveletTreeHuffman<CharT>::rank(char_type c, size_t pos) const
{
    if (c >= leaves_.size() || !leaves_[c]) return 0;

    pos = std::min(pos, length());

    uint64_t code = code_map_[c];
    WaveletTreeNode *walk = root_;

    for (size_t level = 0; pos > 0; ++level)
    {
        if (code & 1ULL << level)
        {
            if (walk->right_)
            {
                pos = walk->rank1(pos);
                walk = walk->right_;
            }
            else
            {
                return walk->rank1(pos);
            }
        }
        else
        {
            if (walk->left_)
            {
                pos = walk->rank0(pos);
                walk = walk->left_;
            }
            else
            {
                return walk->rank0(pos);
            }
        }
    }

    return 0;
}

template <class CharT>
size_t WaveletTreeHuffman<CharT>::select(char_type c, size_t rank) const
{
    if (!leaves_[c]) return -1;

    WaveletTreeNode *walk = leaves_[c];
    if (!walk) return -1;

    bool bit = (walk->c1_ == c);

    for (; walk->parent_; walk = walk->parent_)
    {
        if ((rank = walk->select(rank, bit)) == (size_t)-1)
            return -1;

        bit = (walk == walk->parent_->right_);
    }

    return walk->select(rank, bit);
}

template <class CharT>
void WaveletTreeHuffman<CharT>::intersect(
        const std::vector<std::pair<size_t, size_t> > &ranges,
        size_t thres,
        size_t max_count,
        std::vector<char_type> &results) const
{
    if (thres > ranges.size()) return;
    if (thres > 0) thres = ranges.size() - thres;

    recursiveIntersect_(root_, ranges, thres, max_count, results);
}

template <class CharT>
void WaveletTreeHuffman<CharT>::recursiveIntersect_(
        const WaveletTreeNode *node,
        const std::vector<std::pair<size_t, size_t> > &ranges,
        size_t thres,
        size_t max_count,
        std::vector<char_type> &results) const
{
    if (results.size() >= max_count) return;

    std::vector<std::pair<size_t, size_t> > zero_ranges, one_ranges;
    zero_ranges.reserve(ranges.size());
    one_ranges.reserve(ranges.size());

    size_t zero_thres = thres, one_thres = thres;
    bool has_zeros = true, has_ones = true;

    size_t rank_start, rank_end;

    for (std::vector<std::pair<size_t, size_t> >::const_iterator it = ranges.begin();
            it != ranges.end(); ++it)
    {
        rank_start = node->rank1(it->first);
        rank_end = node->rank1(it->second);

        if (has_zeros)
        {
            if (it->first - rank_start >= it->second - rank_end)
            {
                if (zero_thres == 0)
                {
                    if (!has_ones) return;
                    has_zeros = false;
                }
                else
                {
                    --zero_thres;
                }
            }
            else
            {
                zero_ranges.push_back(std::make_pair(it->first - rank_start, it->second - rank_end));
            }
        }

        if (has_ones)
        {
            if (rank_start >= rank_end)
            {
                if (one_thres == 0)
                {
                    if (!has_zeros) return;
                    has_ones = false;
                }
                else
                {
                    --one_thres;
                }
            }
            else
            {
                one_ranges.push_back(std::make_pair(rank_start, rank_end));
            }
        }
    }

    if (has_zeros)
    {
        if (node->left_)
        {
            recursiveIntersect_(node->left_, zero_ranges, zero_thres, max_count, results);
        }
        else
        {
            results.push_back(node->c0_);
        }
    }

    if (results.size() >= max_count) return;

    if (has_ones)
    {
        if (node->right_)
        {
            recursiveIntersect_(node->right_, one_ranges, one_thres, max_count, results);
        }
        else
        {
            results.push_back(node->c1_);
        }
    }
}

template <class CharT>
void WaveletTreeHuffman<CharT>::topKUnion(
        const std::vector<boost::tuple<size_t, size_t, double> > &ranges,
        size_t topK,
        std::vector<std::pair<double, char_type> > &results) const
{
    if (topK == 0) return;

    boost::priority_deque<PatternList *> ranges_queue;
    ranges_queue.push(new PatternList(0, (char_type)0, root_, ranges));

    if (ranges_queue.top()->score_ == 0.0)
    {
        delete ranges_queue.top();
        return;
    }

    results.reserve(topK);

    size_t max_queue_size = std::max(topK, DEFAULT_TOP_K);

    std::vector<PatternList *> recyc_queue(max_queue_size + 1);
    for (size_t i = 0; i < recyc_queue.size(); ++i)
    {
        recyc_queue[i] = new PatternList(0, 0, NULL, ranges.size());
    }
    std::deque<PatternList *> top_queue;

    PatternList *top_ranges;
    PatternList *zero_ranges, *one_ranges;
    size_t rank_start, rank_end;
    const WaveletTreeNode *node;

    while (results.size() < topK)
    {
        if (!top_queue.empty())
        {
            top_ranges = top_queue.back();
            top_queue.pop_back();
        }
        else if (!ranges_queue.empty())
        {
            top_ranges = ranges_queue.top();
            ranges_queue.pop_top();
        }
        else
        {
            break;
        }

        if (!top_ranges->node_)
        {
            results.push_back(std::make_pair(top_ranges->score_, top_ranges->sym_));
            recyc_queue.push_back(top_ranges);
            continue;
        }

        node = top_ranges->node_;

        zero_ranges = recyc_queue.back();
        zero_ranges->reset(top_ranges->level_ + 1, node->c0_, node->left_);
        recyc_queue.pop_back();

        one_ranges = recyc_queue.back();
        one_ranges->reset(zero_ranges->level_, node->c1_, node->right_);
        recyc_queue.pop_back();

        for (std::vector<boost::tuple<size_t, size_t, double> >::const_iterator it = top_ranges->patterns_.begin();
                it != top_ranges->patterns_.end(); ++it)
        {
            rank_start = node->rank1(it->get<0>());
            rank_end = node->rank1(it->get<1>());

            zero_ranges->addPattern(boost::make_tuple(it->get<0>() - rank_start, it->get<1>() - rank_end, it->get<2>()));
            one_ranges->addPattern(boost::make_tuple(rank_start, rank_end, it->get<2>()));
        }

        zero_ranges->calcScore();
        if (zero_ranges->score_ == 0.0)
        {
            recyc_queue.push_back(zero_ranges);
        }
        else if (zero_ranges->score_ == top_ranges->score_ || (top_queue.empty() && (ranges_queue.empty() || zero_ranges->score_ >= ranges_queue.top()->score_)))
        {
            if (zero_ranges->node_)
            {
                top_queue.push_back(zero_ranges);
            }
            else
            {
                results.push_back(std::make_pair(zero_ranges->score_, zero_ranges->sym_));
                recyc_queue.push_back(zero_ranges);
            }
        }
        else
        {
            ranges_queue.push(zero_ranges);
        }

        one_ranges->calcScore();
        if (one_ranges->score_ == 0.0)
        {
            recyc_queue.push_back(one_ranges);
        }
        else if (one_ranges->score_ == top_ranges->score_ || (top_queue.empty() && (ranges_queue.empty() || one_ranges->score_ >= ranges_queue.top()->score_)))
        {
            if (one_ranges->node_)
            {
                top_queue.push_back(one_ranges);

                if (top_queue.size() > max_queue_size)
                {
                    recyc_queue.push_back(top_queue.front());
                    top_queue.pop_front();
                }
                else if (top_queue.size() + ranges_queue.size() > max_queue_size)
                {
                    recyc_queue.push_back(ranges_queue.bottom());
                    ranges_queue.pop_bottom();
                }
            }
            else
            {
                results.push_back(std::make_pair(one_ranges->score_, one_ranges->sym_));
                recyc_queue.push_back(one_ranges);
            }
        }
        else if (top_queue.size() == max_queue_size || (top_queue.size() + ranges_queue.size() == max_queue_size && one_ranges->score_ < ranges_queue.bottom()->score_))
        {
            recyc_queue.push_back(one_ranges);
        }
        else
        {
            ranges_queue.push(one_ranges);

            if (top_queue.size() + ranges_queue.size() > max_queue_size)
            {
                recyc_queue.push_back(ranges_queue.bottom());
                ranges_queue.pop_bottom();
            }
        }

        recyc_queue.push_back(top_ranges);
    }

    for (size_t i = 0; i < ranges_queue.size(); ++i)
    {
        delete ranges_queue.get(i);
    }

    for (size_t i = 0; i < recyc_queue.size(); ++i)
    {
        delete recyc_queue[i];
    }

    for (size_t i = 0; i < top_queue.size(); ++i)
    {
        delete top_queue[i];
    }
}

template <class CharT>
void WaveletTreeHuffman<CharT>::topKUnionWithFilters(
        const std::vector<std::pair<size_t, size_t> > &filters,
        const std::vector<boost::tuple<size_t, size_t, double> > &ranges,
        size_t topK,
        std::vector<std::pair<double, char_type> > &results) const
{
    if (topK == 0) return;

    boost::priority_deque<FilteredPatternList *> ranges_queue;
    ranges_queue.push(new FilteredPatternList(0, (char_type)0, root_, filters, ranges));

    if (ranges_queue.top()->score_ == 0.0)
    {
        delete ranges_queue.top();
        return;
    }

    results.reserve(topK);

    size_t max_queue_size = std::max(topK, DEFAULT_TOP_K);

    std::vector<FilteredPatternList *> recyc_queue(max_queue_size + 1);
    for (size_t i = 0; i < recyc_queue.size(); ++i)
    {
        recyc_queue[i] = new FilteredPatternList(0, 0, NULL, filters.size(), ranges.size());
    }
    std::deque<FilteredPatternList *> top_queue;

    FilteredPatternList *top_ranges;
    FilteredPatternList *zero_ranges, *one_ranges;
    size_t rank_start, rank_end;
    const WaveletTreeNode *node;

    while (results.size() < topK)
    {
        if (!top_queue.empty())
        {
            top_ranges = top_queue.back();
            top_queue.pop_back();
        }
        else if (!ranges_queue.empty())
        {
            top_ranges = ranges_queue.top();
            ranges_queue.pop_top();
        }
        else
        {
            break;
        }

        if (!top_ranges->node_)
        {
            results.push_back(std::make_pair(top_ranges->score_, top_ranges->sym_));
            recyc_queue.push_back(top_ranges);
            continue;
        }

        node = top_ranges->node_;

        zero_ranges = recyc_queue.back();
        zero_ranges->reset(top_ranges->level_ + 1, node->c0_, node->left_);
        recyc_queue.pop_back();

        one_ranges = recyc_queue.back();
        one_ranges->reset(zero_ranges->level_, node->c1_, node->right_);
        recyc_queue.pop_back();

        for (std::vector<std::pair<size_t, size_t> >::const_iterator it = top_ranges->filters_.begin();
                it != top_ranges->filters_.end(); ++it)
        {
            rank_start = node->rank1(it->first);
            rank_end = node->rank1(it->second);

            zero_ranges->addFilter(std::make_pair(it->first - rank_start, it->second - rank_end));
            one_ranges->addFilter(std::make_pair(rank_start, rank_end));
        }

        if (zero_ranges->filters_.empty())
        {
            recyc_queue.push_back(zero_ranges);
            zero_ranges = NULL;
        }
        if (one_ranges->filters_.empty())
        {
            recyc_queue.push_back(one_ranges);
            one_ranges = NULL;
        }
        if (!zero_ranges && !one_ranges)
        {
            recyc_queue.push_back(top_ranges);
            continue;
        }

        for (std::vector<boost::tuple<size_t, size_t, double> >::const_iterator it = top_ranges->patterns_.begin();
                it != top_ranges->patterns_.end(); ++it)
        {
            rank_start = node->rank1(it->get<0>());
            rank_end = node->rank1(it->get<1>());

            if (zero_ranges)
            {
                zero_ranges->addPattern(boost::make_tuple(it->get<0>() - rank_start, it->get<1>() - rank_end, it->get<2>()));
            }
            if (one_ranges)
            {
                one_ranges->addPattern(boost::make_tuple(rank_start, rank_end, it->get<2>()));
            }
        }

        if (zero_ranges)
        {
            zero_ranges->calcScore();
            if (zero_ranges->score_ == 0.0)
            {
                recyc_queue.push_back(zero_ranges);
            }
            else if (zero_ranges->score_ == top_ranges->score_ || (top_queue.empty() && (ranges_queue.empty() || zero_ranges->score_ >= ranges_queue.top()->score_)))
            {
                if (zero_ranges->node_)
                {
                    top_queue.push_back(zero_ranges);
                }
                else
                {
                    results.push_back(std::make_pair(zero_ranges->score_, zero_ranges->sym_));
                    recyc_queue.push_back(zero_ranges);
                }
            }
            else
            {
                ranges_queue.push(zero_ranges);
            }
        }

        if (one_ranges)
        {
            one_ranges->calcScore();
            if (one_ranges->score_ == 0.0)
            {
                recyc_queue.push_back(one_ranges);
            }
            else if (one_ranges->score_ == top_ranges->score_ || (top_queue.empty() && (ranges_queue.empty() || one_ranges->score_ >= ranges_queue.top()->score_)))
            {
                if (one_ranges->node_)
                {
                    top_queue.push_back(one_ranges);

                    if (top_queue.size() > max_queue_size)
                    {
                        recyc_queue.push_back(top_queue.front());
                        top_queue.pop_front();
                    }
                    else if (top_queue.size() + ranges_queue.size() > max_queue_size)
                    {
                        recyc_queue.push_back(ranges_queue.bottom());
                        ranges_queue.pop_bottom();
                    }
                }
                else
                {
                    results.push_back(std::make_pair(one_ranges->score_, one_ranges->sym_));
                    recyc_queue.push_back(one_ranges);
                }
            }
            else if (top_queue.size() == max_queue_size || (top_queue.size() + ranges_queue.size() == max_queue_size && one_ranges->score_ < ranges_queue.bottom()->score_))
            {
                recyc_queue.push_back(one_ranges);
            }
            else
            {
                ranges_queue.push(one_ranges);

                if (top_queue.size() + ranges_queue.size() > max_queue_size)
                {
                    recyc_queue.push_back(ranges_queue.bottom());
                    ranges_queue.pop_bottom();
                }
            }
        }

        recyc_queue.push_back(top_ranges);
    }

    for (size_t i = 0; i < ranges_queue.size(); ++i)
    {
        delete ranges_queue.get(i);
    }

    for (size_t i = 0; i < recyc_queue.size(); ++i)
    {
        delete recyc_queue[i];
    }

    for (size_t i = 0; i < top_queue.size(); ++i)
    {
        delete top_queue[i];
    }
}

template <class CharT>
void WaveletTreeHuffman<CharT>::topKUnionWithAuxFilters(
        const std::vector<FilterList<self_type> *> &aux_filters,
        const std::vector<std::pair<size_t, size_t> > &filters,
        const std::vector<boost::tuple<size_t, size_t, double> > &ranges,
        size_t topK,
        std::vector<std::pair<double, char_type> > &results) const
{
    if (topK == 0) return;

    boost::priority_deque<AuxFilteredPatternList<self_type> *> ranges_queue;
    ranges_queue.push(new AuxFilteredPatternList<self_type>(0, (char_type)0, root_, aux_filters, filters, ranges));

    if (ranges_queue.top()->score_ == 0.0)
    {
        delete ranges_queue.top();
        return;
    }

    results.reserve(topK);

    size_t max_queue_size = std::max(topK, DEFAULT_TOP_K);
    size_t max_filter_size = 0;
    for (size_t i = 0; i < aux_filters.size(); ++i)
    {
        max_filter_size = std::max(max_filter_size, aux_filters[i]->filters_.size());
    }

    std::vector<AuxFilteredPatternList<self_type> *> recyc_queue(max_queue_size + 1);
    for (size_t i = 0; i < recyc_queue.size(); ++i)
    {
        recyc_queue[i] = new AuxFilteredPatternList<self_type>(0, 0, NULL, aux_filters.size(), ranges.size(), max_filter_size);
    }
    std::deque<AuxFilteredPatternList<self_type> *> top_queue;

    AuxFilteredPatternList<self_type> *top_ranges;
    AuxFilteredPatternList<self_type> *zero_ranges, *one_ranges;
    FilterList<self_type> *zero_filter, *one_filter;
    size_t rank_start, rank_end;
    const WaveletTreeNode *node;

    while (results.size() < topK)
    {
        if (!top_queue.empty())
        {
            top_ranges = top_queue.back();
            top_queue.pop_back();
        }
        else if (!ranges_queue.empty())
        {
            top_ranges = ranges_queue.top();
            ranges_queue.pop_top();
        }
        else
        {
            break;
        }

        if (!top_ranges->node_)
        {
            results.push_back(std::make_pair(top_ranges->score_, top_ranges->sym_));
            recyc_queue.push_back(top_ranges);
            continue;
        }

        node = top_ranges->node_;

        zero_ranges = recyc_queue.back();
        zero_ranges->reset(top_ranges->level_ + 1, node->c0_, node->left_);
        recyc_queue.pop_back();

        one_ranges = recyc_queue.back();
        one_ranges->reset(zero_ranges->level_, node->c1_, node->right_);
        recyc_queue.pop_back();

        for (typename std::vector<FilterList<self_type> *>::const_iterator it = top_ranges->aux_filters_.begin();
                it != top_ranges->aux_filters_.end(); ++it)
        {
            node = (*it)->node_;

            if (zero_ranges)
            {
                zero_filter = zero_ranges->recyc_aux_filters_.back();
                zero_filter->reset((*it)->tree_, node->left_);
                zero_ranges->recyc_aux_filters_.pop_back();
            }
            if (one_ranges)
            {
                one_filter = one_ranges->recyc_aux_filters_.back();
                one_filter->reset((*it)->tree_, node->right_);
                one_ranges->recyc_aux_filters_.pop_back();
            }

            for (std::vector<std::pair<size_t, size_t> >::const_iterator fit = (*it)->filters_.begin();
                    fit != (*it)->filters_.end(); ++fit)
            {
                rank_start = node->rank1(fit->first);
                rank_end = node->rank1(fit->second);

                if (zero_ranges)
                {
                    zero_filter->addFilter(std::make_pair(fit->first - rank_start, fit->second - rank_end));
                }
                if (one_ranges)
                {
                    one_filter->addFilter(std::make_pair(rank_start, rank_end));
                }
            }

            if (zero_ranges && !zero_ranges->addAuxFilter(zero_filter))
            {
                recyc_queue.push_back(zero_ranges);
                zero_ranges = NULL;
                if (!one_ranges) break;
            }
            if (one_ranges && !one_ranges->addAuxFilter(one_filter))
            {
                recyc_queue.push_back(one_ranges);
                one_ranges = NULL;
                if (!zero_ranges) break;
            }
        }

        if (!zero_ranges && !one_ranges)
        {
            recyc_queue.push_back(top_ranges);
            continue;
        }

        node = top_ranges->node_;

        for (std::vector<boost::tuple<size_t, size_t, double> >::const_iterator it = top_ranges->patterns_.begin();
                it != top_ranges->patterns_.end(); ++it)
        {
            rank_start = node->rank1(it->get<0>());
            rank_end = node->rank1(it->get<1>());

            if (zero_ranges)
            {
                zero_ranges->addPattern(boost::make_tuple(it->get<0>() - rank_start, it->get<1>() - rank_end, it->get<2>()));
            }
            if (one_ranges)
            {
                one_ranges->addPattern(boost::make_tuple(rank_start, rank_end, it->get<2>()));
            }
        }

        if (zero_ranges)
        {
            zero_ranges->calcScore();
            if (zero_ranges->score_ == 0.0)
            {
                recyc_queue.push_back(zero_ranges);
            }
            else if (zero_ranges->score_ == top_ranges->score_ || (top_queue.empty() && (ranges_queue.empty() || zero_ranges->score_ >= ranges_queue.top()->score_)))
            {
                if (zero_ranges->node_)
                {
                    top_queue.push_back(zero_ranges);
                }
                else
                {
                    results.push_back(std::make_pair(zero_ranges->score_, zero_ranges->sym_));
                    recyc_queue.push_back(zero_ranges);
                }
            }
            else
            {
                ranges_queue.push(zero_ranges);
            }
        }

        if (one_ranges)
        {
            one_ranges->calcScore();
            if (one_ranges->score_ == 0.0)
            {
                recyc_queue.push_back(one_ranges);
            }
            else if (one_ranges->score_ == top_ranges->score_ || (top_queue.empty() && (ranges_queue.empty() || one_ranges->score_ >= ranges_queue.top()->score_)))
            {
                if (one_ranges->node_)
                {
                    top_queue.push_back(one_ranges);

                    if (top_queue.size() > max_queue_size)
                    {
                        recyc_queue.push_back(top_queue.front());
                        top_queue.pop_front();
                    }
                    else if (top_queue.size() + ranges_queue.size() > max_queue_size)
                    {
                        recyc_queue.push_back(ranges_queue.bottom());
                        ranges_queue.pop_bottom();
                    }
                }
                else
                {
                    results.push_back(std::make_pair(one_ranges->score_, one_ranges->sym_));
                    recyc_queue.push_back(one_ranges);
                }
            }
            else if (top_queue.size() == max_queue_size || (top_queue.size() + ranges_queue.size() == max_queue_size && one_ranges->score_ < ranges_queue.bottom()->score_))
            {
                recyc_queue.push_back(one_ranges);
            }
            else
            {
                ranges_queue.push(one_ranges);

                if (top_queue.size() + ranges_queue.size() > max_queue_size)
                {
                    recyc_queue.push_back(ranges_queue.bottom());
                    ranges_queue.pop_bottom();
                }
            }
        }

        recyc_queue.push_back(top_ranges);
    }

    for (size_t i = 0; i < ranges_queue.size(); ++i)
    {
        delete ranges_queue.get(i);
    }

    for (size_t i = 0; i < recyc_queue.size(); ++i)
    {
        delete recyc_queue[i];
    }

    for (size_t i = 0; i < top_queue.size(); ++i)
    {
        delete top_queue[i];
    }
}

template <class CharT>
size_t WaveletTreeHuffman<CharT>::getOcc(char_type c) const
{
    if (c < occ_.size()) return occ_[c];
    return occ_.back();
}

template <class CharT>
WaveletTreeNode *WaveletTreeHuffman<CharT>::getRoot() const
{
    return root_;
}

template <class CharT>
size_t WaveletTreeHuffman<CharT>::length() const
{
    return root_ ? root_->length() : 0;
}

template <class CharT>
size_t WaveletTreeHuffman<CharT>::allocSize() const
{
    return sizeof(WaveletTreeHuffman<char_type>)
        + sizeof(occ_[0]) * occ_.size()
        + sizeof(code_map_[0]) * code_map_.size()
        + getTreeSize_(root_);
}

template <class CharT>
size_t WaveletTreeHuffman<CharT>::getTreeSize_(const WaveletTreeNode *node) const
{
    if (!node) return 0;
    return node->allocSize() + getTreeSize_(node->left_) + getTreeSize_(node->right_);
}

template <class CharT>
void WaveletTreeHuffman<CharT>::save(std::ostream &ostr) const
{
    WaveletTree<CharT>::save(ostr);

    ostr.write((const char *)&occ_[0], sizeof(occ_[0]) * occ_.size());
    ostr.write((const char *)&code_map_[0], sizeof(code_map_[0]) * code_map_.size());

    if (root_)
    {
        uint32_t flag = 1U;
        ostr.write((const char *)&flag, sizeof(flag));
        saveTree_(ostr, root_);
    }
    else
    {
        uint32_t flag = 0U;
        ostr.write((const char *)&flag, sizeof(flag));
    }
}

template <class CharT>
void WaveletTreeHuffman<CharT>::saveTree_(std::ostream &ostr, const WaveletTreeNode *node) const
{
    node->save(ostr);

    uint32_t flag = (node->left_ ? 1U : 0U) | (node->right_ ? 2U : 0U);
    ostr.write((const char *)&flag, sizeof(flag));

    if (node->left_) saveTree_(ostr, node->left_);
    if (node->right_) saveTree_(ostr, node->right_);
}

template <class CharT>
void WaveletTreeHuffman<CharT>::load(std::istream &istr)
{
    WaveletTree<CharT>::load(istr);

    if (root_) deleteTree_(root_);

    this->alphabet_bit_num_ = bits(this->alphabet_num_ - 1);

    occ_.resize((1 << this->alphabet_bit_num_) + 1);
    istr.read((char *)&occ_[0], sizeof(occ_[0]) * occ_.size());
    code_map_.resize(this->alphabet_num_);
    istr.read((char *)&code_map_[0], sizeof(code_map_[0]) * code_map_.size());

    uint32_t flag = 0;
    istr.read((char *)&flag, sizeof(flag));
    if (flag)
    {
        root_ = new WaveletTreeNode(this->support_select_, this->dense_);
        leaves_.resize(this->alphabet_num_);
        loadTree_(istr, root_);
    }
}

template <class CharT>
void WaveletTreeHuffman<CharT>::loadTree_(std::istream &istr, WaveletTreeNode *node)
{
    node->load(istr);

    uint32_t flag = 0;
    istr.read((char *)&flag, sizeof(flag));

    if (flag & 1U)
    {
        node->left_ = new WaveletTreeNode(this->support_select_, this->dense_);
        node->left_->parent_ = node;
        loadTree_(istr, node->left_);
    }
    else
    {
        leaves_[node->c0_] = node;
    }

    if (flag & 2U)
    {
        node->right_ = new WaveletTreeNode(this->support_select_, this->dense_);
        node->right_->parent_ = node;
        loadTree_(istr, node->right_);
    }
    else
    {
        leaves_[node->c1_] = node;
    }
}

}
}

NS_IZENELIB_AM_END

#endif
