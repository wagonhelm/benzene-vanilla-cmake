//----------------------------------------------------------------------------
/** @file HexUctPolicy.cpp
    
    @todo Pattern statistics are collected for each thread. Add
    functionality to combine the stats from each thread before
    displaying them. Only do this if pattern statistics are actually
    required, obviously.
 */
//----------------------------------------------------------------------------

#include "SgSystem.h"
#include "HexUctPolicy.hpp"
#include "Misc.hpp"
#include "PatternState.hpp"

using namespace benzene;

//----------------------------------------------------------------------------

namespace 
{

/** Shuffle a vector with the given random number generator. 
    @todo Refactor this out somewhere.
*/
template<typename T>
void ShuffleVector(std::vector<T>& v, SgRandom& random)
{
    for (int i = static_cast<int>(v.size() - 1); i > 0; --i) 
    {
        int j = random.Int(i+1);
        std::swap(v[i], v[j]);
    }
}

/** Returns true 'percent' of the time. */
bool PercentChance(int percent, SgRandom& random)
{
    if (percent >= 100) 
        return true;
    unsigned int threshold = random.PercentageThreshold(percent);
    return random.RandomEvent(threshold);
}

} // annonymous namespace

//----------------------------------------------------------------------------

HexUctPolicyConfig::HexUctPolicyConfig()
    : patternHeuristic(true),
      responseHeuristic(false),
      pattern_update_radius(1),
      pattern_check_percent(100),
      response_threshold(100)
{
}

//----------------------------------------------------------------------------

HexUctSharedPolicy::HexUctSharedPolicy()
    : m_config()
{
    LoadPatterns();
}

HexUctSharedPolicy::~HexUctSharedPolicy()
{
}

void HexUctSharedPolicy::LoadPatterns()
{
    LoadPlayPatterns();
}

void HexUctSharedPolicy::LoadPlayPatterns()
{
    std::ifstream inFile;
    try {
        std::string file = MiscUtil::OpenFile("mohex-patterns.txt", inFile);
        LogConfig() << "HexUctSharedPolicy: reading patterns from '" 
                    << file << "'.\n";
    }
    catch (BenzeneException& e) {
        throw BenzeneException() << "HexUctSharedPolicy: " << e.what();
    }
    std::vector<Pattern> patterns;
    Pattern::LoadPatternsFromStream(inFile, patterns);
    LogConfig() << "HexUctSharedPolicy: parsed " << patterns.size() 
                << " patterns.\n";
    BenzeneAssert(m_patterns[BLACK].empty()); // Can only load patterns once!
    for (std::size_t i = 0; i < patterns.size(); ++i) 
    {
        Pattern p = patterns[i];
        switch(p.GetType()) 
        {
        case Pattern::MOHEX:
            m_patterns[BLACK].push_back(p);
            p.FlipColors();
            m_patterns[WHITE].push_back(p);
            break;
        default:
            throw BenzeneException() 
                  << "HexUctSharedPolicy: unknown pattern type '" 
                  << p.GetType() << "'\n";
        }
    }
    for (BWIterator color; color; ++color)
        m_hash_patterns[*color].Hash(m_patterns[*color]);
}

//----------------------------------------------------------------------------

HexUctPolicy::HexUctPolicy(const HexUctSharedPolicy* shared)
    : m_shared(shared)
#if COLLECT_PATTERN_STATISTICS
    , m_statistics()
#endif
{
}

HexUctPolicy::~HexUctPolicy()
{
}

//----------------------------------------------------------------------------

/** @todo Pass initialial tree and initialize off of that? */
void HexUctPolicy::InitializeForSearch()
{
    for (int i = 0; i < BITSETSIZE; ++i)
    {
        m_response[BLACK][i].clear();
        m_response[WHITE][i].clear();
    }
}

void HexUctPolicy::InitializeForRollout(const StoneBoard& brd)
{
    BitsetUtil::BitsetToVector(brd.GetEmpty(), m_moves);
    ShuffleVector(m_moves, m_random);
}

HexPoint HexUctPolicy::GenerateMove(PatternState& pastate, 
                                    HexColor toPlay, 
                                    HexPoint lastMove)
{
    HexPoint move = INVALID_POINT;
    bool pattern_move = false;
    const HexUctPolicyConfig& config = m_shared->Config();
#if COLLECT_PATTERN_STATISTICS
    HexUctPolicyStatistics& stats = m_statistics;
#endif

    // patterns applied probabilistically (if heuristic is turned on)
    if (config.patternHeuristic 
        && PercentChance(config.pattern_check_percent, m_random))
    {
        move = GeneratePatternMove(pastate, toPlay, lastMove);
    }
    
    if (move == INVALID_POINT
        && config.responseHeuristic)
    {
        move = GenerateResponseMove(toPlay, lastMove, pastate.Board());
    }

    // select random move if invalid point from pattern heuristic
    if (move == INVALID_POINT) 
    {
#if COLLECT_PATTERN_STATISTICS
	stats.random_moves++;
#endif
        move = GenerateRandomMove(pastate.Board());
    } 
    else 
    {
	pattern_move = true;
#if COLLECT_PATTERN_STATISTICS
        stats.pattern_moves++;
#endif
    }
    
    BenzeneAssert(pastate.Board().IsEmpty(move));
#if COLLECT_PATTERN_STATISTICS
    stats.total_moves++;
#endif
    return move;
}

#if COLLECT_PATTERN_STATISTICS
std::string HexUctPolicy::DumpStatistics()
{
    std::ostringstream os;

    os << std::endl;
    os << "Pattern statistics:" << std::endl;
    os << std::setw(12) << "Name" << "  "
       << std::setw(10) << "Black" << " "
       << std::setw(10) << "White" << " "
       << std::setw(10) << "Black" << " "
       << std::setw(10) << "White" << std::endl;

    os << "     ------------------------------------------------------" 
       << std::endl;

    HexUctPolicyStatistics& stats = Statistics();
    for (unsigned i=0; i<m_patterns[BLACK].size(); ++i) {
        os << std::setw(12) << m_patterns[BLACK][i].getName() << ": "
           << std::setw(10) << stats.pattern_counts[BLACK]
            [&m_patterns[BLACK][i]] << " "
           << std::setw(10) << stats.pattern_counts[WHITE]
            [&m_patterns[WHITE][i]] << " " 
           << std::setw(10) << stats.pattern_picked[BLACK]
            [&m_patterns[BLACK][i]] << " "
           << std::setw(10) << stats.pattern_picked[WHITE]
            [&m_patterns[WHITE][i]]
           << std::endl;
    }

    os << "     ------------------------------------------------------" 
       << std::endl;

    os << std::endl;
    os << std::setw(12) << "Pattern" << ": " 
       << std::setw(10) << stats.pattern_moves << " "
       << std::setw(10) << std::setprecision(3) << 
        stats.pattern_moves*100.0/stats.total_moves << "%" 
       << std::endl;
    os << std::setw(12) << "Random" << ": " 
       << std::setw(10) << stats.random_moves << " "
       << std::setw(10) << std::setprecision(3) << 
        stats.random_moves*100.0/stats.total_moves << "%"  
       << std::endl;
    os << std::setw(12) << "Total" << ": " 
       << std::setw(10) << stats.total_moves << std::endl;

    os << std::endl;
    
    return os.str();
}
#endif

//--------------------------------------------------------------------------

HexPoint HexUctPolicy::GenerateResponseMove(HexColor toPlay, HexPoint lastMove,
                                            const StoneBoard& brd)
{
    std::size_t num = m_response[toPlay][lastMove].size();
    if (num > m_shared->Config().response_threshold)
    {
        HexPoint move = m_response[toPlay][lastMove][m_random.Int(num)];
        if (brd.IsEmpty(move))
            return move;
    }
    return INVALID_POINT;
}

/** Selects random move among the empty cells on the board. */
HexPoint HexUctPolicy::GenerateRandomMove(const StoneBoard& brd)
{
    HexPoint ret = INVALID_POINT;
    while (true) 
    {
	BenzeneAssert(!m_moves.empty());
        ret = m_moves.back();
        m_moves.pop_back();
        if (brd.IsEmpty(ret))
            break;
    }
    return ret;
}

/** Randomly picks a pattern move from the set of patterns that hit
    the last move, weighted by the pattern's weight. 
    If no pattern matches, returns INVALID_POINT. */
HexPoint HexUctPolicy::PickRandomPatternMove(const PatternState& pastate, 
                                             const HashedPatternSet& patterns, 
                                             HexColor toPlay,
                                             HexPoint lastMove)
{
    UNUSED(toPlay);

    if (lastMove == INVALID_POINT)
	return INVALID_POINT;
    
    int num = 0;
    int patternIndex[MAX_VOTES];
    HexPoint patternMoves[MAX_VOTES];

    PatternHits hits;
    pastate.MatchOnCell(patterns, lastMove, PatternState::MATCH_ALL, hits);

    for (unsigned i = 0; i < hits.size(); ++i) 
    {
#if COLLECT_PATTERN_STATISTICS
        // record that this pattern hit
        m_shared->Statistics().pattern_counts[toPlay][hits[i].pattern()]++;
#endif
            
        // number of entries added to array is equal to the pattern's weight
        for (int j = 0; j < hits[i].GetPattern()->GetWeight(); ++j) 
        {
            patternIndex[num] = i;
            patternMoves[num] = hits[i].Moves1()[0];
            num++;
            BenzeneAssert(num < MAX_VOTES);
        }
    }
    
    // abort if no pattern hit
    if (num == 0) 
        return INVALID_POINT;
    
    // select move at random (biased according to number of entries)
    int i = m_random.Int(num);

#if COLLECT_PATTERN_STATISTICS
    m_shared->Statistics().pattern_picked
        [toPlay][hits[patternIndex[i]].pattern()]++;
#endif

    return patternMoves[i];
}

/** Uses PickRandomPatternMove() with the shared PlayPatterns(). */
HexPoint HexUctPolicy::GeneratePatternMove(const PatternState& pastate, 
                                           HexColor toPlay, 
                                           HexPoint lastMove)
{
    return PickRandomPatternMove(pastate, m_shared->PlayPatterns(toPlay),
                                 toPlay, lastMove);
}

//----------------------------------------------------------------------------
