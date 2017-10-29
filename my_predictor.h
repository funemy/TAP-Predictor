// my_predictor.h
// This file contains a sample my_predictor class.
// It has a simple 32,768-entry gshare with a history length of 15 and a
// simple direct-mapped branch target buffer for indirect branch prediction.
#include <map>

// 4 sub predictor, 16 target entries in total
#define SUB_PREDICTOR 4
#define HISTORY_LENGTH	15
#define TABLE_BITS	15

// sub predictor definition
class SubPredictor {
public:
//	history register for sub predictor
	unsigned int history;
//  PHT for sub predictor
	unsigned char tab[1<<TABLE_BITS];
	SubPredictor (void) : history(0) {
        memset(tab, 0, sizeof(tab));
	}
};

// Branch target entries
typedef struct entries {
    unsigned int addr[1<<SUB_PREDICTOR];
//	lru array, storing the index of addresses
	int lru[1<<SUB_PREDICTOR] = {15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0};
} entries;

class my_update : public branch_update {
public:
	unsigned int index;
	unsigned int tap;
    unsigned int sub_indexes[SUB_PREDICTOR];
};

class my_predictor : public branch_predictor {
	int assoc = 1 << SUB_PREDICTOR;
	entries ent;
public:
	my_update u;
	branch_info bi;
//	Target address pointer
	unsigned int sub_index;
	unsigned int tap;
	unsigned int history;
	unsigned char tab[1<<TABLE_BITS];
    SubPredictor *sub_predctor_set[SUB_PREDICTOR];
//	BTB, using lru replacement
	std::map<unsigned int, entries> targets;

	my_predictor (void) : history(0), tap(0) {
		memset (tab, 0, sizeof (tab));
//      instantiate sub predictor
		for (auto &sp : sub_predctor_set) {
			sp = new SubPredictor();
		}
	}

	branch_update *predict (branch_info & b) {
		bi = b;
//      conditional branch predict
		if (b.br_flags & BR_CONDITIONAL) {
			u.index = 
				  (history << (TABLE_BITS - HISTORY_LENGTH)) 
				^ (b.address & ((1<<TABLE_BITS)-1));
			u.direction_prediction (tab[u.index] >> 1);
		} else {
			u.direction_prediction (true);
		}
//		indirect branch predict
		if (b.br_flags & BR_INDIRECT) {
			tap = 0;
//			using sub predictor to predict each bit of TAP
			SubPredictor *sp;
			for (int i = 0; i < SUB_PREDICTOR; i++) {
				sp = sub_predctor_set[i];
                sub_index = (sp->history << (TABLE_BITS - HISTORY_LENGTH))
						^ (b.address & ((1<<TABLE_BITS)-1));
				tap += ((sp->tab[sub_index]) >> 1);
				tap <<= 1;
                u.sub_indexes[i] = sub_index;
			}
			u.tap = tap;
            printf("tap: %d\n",tap);
//			store the predict target
			u.target_prediction( targets[b.address].addr[tap] );
		}
		return &u;
	}

	void update (branch_update *u, bool taken, unsigned int target) {
//		conditional branch update
//		update GHR
		if (bi.br_flags & BR_CONDITIONAL) {
			unsigned char *c = &tab[((my_update*)u)->index];
			if (taken) {
				if (*c < 3) (*c)++;
			} else {
				if (*c > 0) (*c)--;
			}
			history <<= 1;
			history |= taken;
			history &= (1<<HISTORY_LENGTH)-1;
		}
//      indirect branch update
		if (bi.br_flags & BR_INDIRECT) {
//			when prediction is different from target
//			update the sub PHT and BTB
            if (target != u->target_prediction()) {
				int i,j,k;
				ent = targets[bi.address];
				for (i = 0; i < assoc; ++i) {
//					the TAP for real target
					if (ent.addr[i] == target) {
//						update LRU array
                        for (k = i; k > 0; k--) {
                            ent.lru[k] = ent.lru[k-1];
						}
						ent.lru[0] = i;
//                      update sub predictor with this TAP
                        unsigned char *c;
//						direction
						bool dir;
//						history
						unsigned int *h;
						for (j = 0; j < SUB_PREDICTOR; j++) {
                            c = &sub_predctor_set[j]->tab[((my_update*)u)->sub_indexes[j]];
                            dir = (i & (1 << (SUB_PREDICTOR - j -1))) >> (SUB_PREDICTOR - j -1);
							h = &sub_predctor_set[j]->history;
							if (dir) {
								if (*c < 3) (*c)++;
							} else {
								if (*c > 0) (*c)--;
							}
							*h <<= 1;
                            *h |= dir;
							*h &= (1<<HISTORY_LENGTH)-1;
						}
						return;
					}
				}
//              if not target match
//				add this target to BTB, with LRU replacement policy
                int r = ent.lru[assoc-1];
                for (k = (assoc-1); k > 0; k--) {
					ent.lru[k] = ent.lru[k-1];
				}
				ent.lru[0] = r;
				ent.addr[r] = target;
//              TODO: sub predictor update
//              update sub predictor with this TAP
				unsigned char *c;
//				direction
				bool dir;
//				history
				unsigned int *h;
				for (j = 0; j < SUB_PREDICTOR; j++) {
					c = &sub_predctor_set[j]->tab[((my_update *) u)->sub_indexes[j]];
					dir = (r & (1 << (SUB_PREDICTOR - j - 1))) >> (SUB_PREDICTOR - j - 1);
					h = &sub_predctor_set[j]->history;
					if (dir) {
						if (*c < 3) (*c)++;
					} else {
						if (*c > 0) (*c)--;
					}
					*h <<= 1;
					*h |= dir;
					*h &= (1 << HISTORY_LENGTH) - 1;
				}
			} else {
                printf("hit\n");
			}
		}
	}
};
