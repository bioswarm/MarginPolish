/*
 * Copyright (C) 2017 by Benedict Paten (benedictpaten@gmail.com)
 *
 * Released under the MIT license, see LICENSE.txt
 */

#include <htslib/vcf.h>
#include "stRPHmm.h"

/*
 * Functions for reference prior probabilities
 */

stReferencePriorProbs *stReferencePriorProbs_constructEmptyProfile(char *referenceName,
        int64_t referenceStart, int64_t length) {
    /*
     * Creates an empty object, with all the profile probabilities set to 0.
     */
    stReferencePriorProbs *referencePriorProbs = st_calloc(1, sizeof(stReferencePriorProbs));
    referencePriorProbs->referenceName = stString_copy(referenceName);
    referencePriorProbs->refStart = referenceStart;
    referencePriorProbs->length = length;
    referencePriorProbs->profileProbs = st_calloc(length*ALPHABET_SIZE, sizeof(uint16_t));
    return referencePriorProbs;
}

void stReferencePriorProbs_destruct(stReferencePriorProbs *referencePriorProbs) {
    free(referencePriorProbs->profileProbs);
    free(referencePriorProbs->referenceName);
    free(referencePriorProbs);
}

static stReferencePriorProbs *getNext(stList *profileSequences) {
    /*
     * Construct an empty stReferencePriorProbs for the next interval of a reference sequence.
     */
    assert(stList_length(profileSequences) > 0);
    stProfileSeq *pSeq = stList_pop(profileSequences);
    char *refName = pSeq->referenceName;
    int64_t refStart = pSeq->refStart;
    int64_t refEnd = pSeq->refStart + pSeq->length;
    while(stList_length(profileSequences) > 0) {
        stProfileSeq *pSeq = stList_peek(profileSequences);

        if(!stString_eq(pSeq->referenceName, refName)) {
            break;
        }
        stList_pop(profileSequences);

        assert(pSeq->refStart <= refStart);
        refStart = pSeq->refStart;
        refEnd = pSeq->refStart + pSeq->length > refEnd ? pSeq->refStart + pSeq->length : refEnd;
    }

    return stReferencePriorProbs_constructEmptyProfile(refName, refStart, refEnd-refStart);
}

stHash *createReferencePriorProbabilities(char *referenceFastaFile, stList *profileSequences,
        stBaseMapper *baseMapper, stRPHmmParameters *params) {
    /*
     * Create a set of stReferencePriorProbs that cover the reference intervals included in the profile sequences.
     * The return value is encoded as a map from the reference sequence name (as a string)
     * to the stReferencePriorProbs.
     */

    // Make map from reference sequence names to reference priors
    stHash *referenceNamesToReferencePriors = stHash_construct3(stHash_stringKey, stHash_stringEqualKey,
            NULL, (void (*)(void *))stReferencePriorProbs_destruct);

    // Copy and sort profile sequences in order
    profileSequences = stList_copy(profileSequences, NULL);
    stList_sort(profileSequences, stRPProfileSeq_cmpFn);

    // Load reference fasta index
    faidx_t *fai = fai_load(referenceFastaFile);
    if ( !fai ) {
        st_errAbort("Could not load fai index of %s.  Maybe you should run 'samtools faidx %s'\n",
                       referenceFastaFile, referenceFastaFile);
    }

    while(stList_length(profileSequences) > 0) {
        // Get next reference prior prob interval
        stReferencePriorProbs *rProbs = getNext(profileSequences);

        // Now get the corresponding reference sequence
        int seqLen;
        char *referenceSeq = fai_fetch(fai, rProbs->referenceName, &seqLen);
        if ( seqLen < 0 ) {
            st_errAbort("Failed to fetch reference sequence %s\n", rProbs->referenceName);
        }

        // Build probability profile
        assert(seqLen >= rProbs->length + rProbs->refStart);
        for(int64_t i=0; i<rProbs->length; i++) {
            int64_t refChar = stBaseMapper_getValueForChar(baseMapper, referenceSeq[i+rProbs->refStart-1]);
            assert(refChar >= 0 && refChar < ALPHABET_SIZE);
            for(int64_t j=0; j<ALPHABET_SIZE; j++) {
                rProbs->profileProbs[i*ALPHABET_SIZE + j] = *getSubstitutionProb(params->hetSubModel, refChar, j);
            }
        }

		assert(stHash_search(referenceNamesToReferencePriors, rProbs->referenceName) == NULL);
        stHash_insert(referenceNamesToReferencePriors, rProbs->referenceName, rProbs);
    }

    // Cleanup
    fai_destroy(fai);
    stList_destruct(profileSequences);

    return referenceNamesToReferencePriors;
}