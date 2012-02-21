#include <stdio.h>
#include <stdlib.h>
#include <iv_avl.h>
#include <iv_list.h>
#include <string.h>

#define PRUNE_STUFF 1

/* data structures **********************************************************/
enum room {
	ROOM_LOBBY = 1,
	ROOM_LEFT_FRONT,
	ROOM_LEFT_BACK,
	ROOM_MIDDLE,
	ROOM_MIDDLE_ON_BUTTON,
	ROOM_PENALTY_BOX,
	ROOM_RIGHT,
	ROOM_RIGHT_BETWEEN_LASERS_AND_FORCE_FIELD,
	ROOM_RIGHT_BEHIND_FORCE_FIELD,
	ROOM_EXIT,
};

enum spot {
	SPOT_UNSET = 1,
	SPOT_LOBBY,
	SPOT_LEFT_BACK,
	SPOT_MIDDLE_MIDDLE,
	SPOT_MIDDLE_LEFT_BACK,
	SPOT_PENALTY_BOX_GROUND,
	SPOT_PENALTY_BOX_LEFT_WALL,
	SPOT_PENALTY_BOX_LONG_WALL,
};

struct actor_state {
	enum room	room;
	enum spot	primary;
	enum spot	secondary;
};

struct state {
	struct actor_state	atlas;
	struct actor_state	p_body;
	enum room	companion_cube;
	enum room	lens_cube_left_back;
	enum room	lens_cube_right;

	int			num;
	int			distance;
	struct state const	*prev;
	char const		*how;
	struct iv_avl_node	an;
	struct iv_list_head	process;
};


/* help conditions **********************************************************/
static int cubes_in_room(struct state const *st, enum room room)
{
	int cubes;

	cubes = 0;
	if (st->companion_cube == room)
		cubes++;
	if (st->lens_cube_left_back == room)
		cubes++;
	if (st->lens_cube_right == room)
		cubes++;

	return cubes;
}

static int have_left_force_field(struct state const *st)
{
	return (cubes_in_room(st, ROOM_LEFT_FRONT) >= 2) ? 0 : 1;
}

static int have_light_bridge(struct state const *st)
{
	if (st->atlas.room == ROOM_MIDDLE_ON_BUTTON)
		return 0;
	if (st->p_body.room == ROOM_MIDDLE_ON_BUTTON)
		return 0;
	if (cubes_in_room(st, ROOM_MIDDLE_ON_BUTTON))
		return 0;

	return 1;
}

static int have_right_force_field(struct state const *st)
{
	return have_light_bridge(st);
}

static int exists_portal_pair(struct state const *st, enum spot a, enum spot b)
{
	if (st->atlas.primary == a && st->atlas.secondary == b)
		return 1;
	if (st->atlas.primary == b && st->atlas.secondary == a)
		return 1;
	if (st->p_body.primary == a && st->p_body.secondary == b)
		return 1;
	if (st->p_body.primary == b && st->p_body.secondary == a)
		return 1;

	return 0;
}

static int have_laser_out_penalty_box_long_wall(struct state const *st)
{
	if (st->lens_cube_left_back != ROOM_LOBBY &&
	    st->lens_cube_right != ROOM_LOBBY)
		return 0;

	if (!exists_portal_pair(st, SPOT_LOBBY, SPOT_PENALTY_BOX_LONG_WALL))
		return 0;

	return 1;
}

static int left_funnel_active(struct state const *st)
{
	if (have_laser_out_penalty_box_long_wall(st))
		return 1;

	return 0;
}

static int have_laser_out_left_back(struct state const *st)
{
	if (st->lens_cube_left_back != ROOM_LOBBY &&
	    st->lens_cube_right != ROOM_LOBBY)
		return 0;

	if (!exists_portal_pair(st, SPOT_LOBBY, SPOT_LEFT_BACK))
		return 0;

	return 1;
}

static int have_laser_cube_in_left_front(struct state const *st)
{
	if (st->lens_cube_left_back == ROOM_LEFT_FRONT)
		return 1;
	if (st->lens_cube_right == ROOM_LEFT_FRONT)
		return 1;

	return 0;
}

static int middle_funnel_active(struct state const *st)
{
	if (have_laser_out_left_back(st) && have_laser_cube_in_left_front(st))
		return 1;

	return 0;
}

static int funnel_coming_out(struct state const *st, enum spot spot)
{
	if (left_funnel_active(st) && exists_portal_pair(st, SPOT_LEFT_BACK, spot))
		return 1;

	if (middle_funnel_active(st) && exists_portal_pair(st, SPOT_MIDDLE_LEFT_BACK, spot))
		return 1;

	return 0;
}

static int funnel_to_pillar(struct state const *st)
{
	return funnel_coming_out(st, SPOT_MIDDLE_MIDDLE);
}

static int funnel_to_exit(struct state const *st)
{
	return funnel_coming_out(st, SPOT_PENALTY_BOX_GROUND);
}


/* foo **********************************************************************/
static struct state initial_state;
static struct iv_avl_tree reachable_states;
static int num_reachable_states;
static struct iv_list_head to_be_processed;

#define COMP(prop)					\
	do {						\
		if (a->prop < b->prop)			\
			return -1;			\
		if (a->prop > b->prop)			\
			return 1;			\
	} while (0)

static int compare_states(struct state const *a, struct state const *b)
{
	COMP(atlas.room);
	COMP(atlas.primary);
	COMP(atlas.secondary);
	COMP(p_body.room);
	COMP(p_body.primary);
	COMP(p_body.secondary);
	COMP(companion_cube);
	COMP(lens_cube_left_back);
	COMP(lens_cube_right);

	return 0;
}

static int
compare_states_avl(struct iv_avl_node const *_a, struct iv_avl_node const *_b)
{
	struct state *a = iv_container_of(_a, struct state, an);
	struct state *b = iv_container_of(_b, struct state, an);

	return compare_states(a, b);
}

static struct state *find_state(struct state const *_st)
{
	struct iv_avl_node *an;

	an = reachable_states.root;
	while (an != NULL) {
		struct state *st;
		int ret;

		st = iv_container_of(an, struct state, an);

		ret = compare_states(_st, st);
		if (ret == 0)
			return st;

		if (ret < 0)
			an = an->left;
		else
			an = an->right;
	}

	return NULL;
}

#if PRUNE_STUFF
static int have_equiv_state_swap_portals(struct state const *st)
{
	struct state tmp;

	tmp = *st;
	if (find_state(&tmp) != NULL)
		return 1;

	tmp = *st;
	tmp.atlas.primary = st->atlas.secondary;
	tmp.atlas.secondary = st->atlas.primary;
	if (find_state(&tmp) != NULL)
		return 1;

	tmp = *st;
	tmp.p_body.primary = st->p_body.secondary;
	tmp.p_body.secondary = st->p_body.primary;
	if (find_state(&tmp) != NULL)
		return 1;

	tmp = *st;
	tmp.atlas.primary = st->atlas.secondary;
	tmp.atlas.secondary = st->atlas.primary;
	tmp.p_body.primary = st->p_body.secondary;
	tmp.p_body.secondary = st->p_body.primary;
	if (find_state(&tmp) != NULL)
		return 1;

	return 0;
}
#endif

static int have_equiv_state(struct state const *st)
{
#if PRUNE_STUFF
	struct state tmp;

	tmp = *st;
	if (have_equiv_state_swap_portals(&tmp))
		return 1;

	tmp = *st;
	tmp.atlas = st->p_body;
	tmp.p_body = st->atlas;
	if (have_equiv_state_swap_portals(&tmp))
		return 1;

	return 0;
#else
	if (find_state(st) != NULL)
		return 1;

	return 0;
#endif
}

static void print_room(enum room r)
{
	switch (r) {
	case ROOM_LOBBY:
		printf("in the lobby\n");
		break;
	case ROOM_LEFT_FRONT:
		printf("left up, before the force field\n");
		break;
	case ROOM_LEFT_BACK:
		printf("left up, behind the force field\n");
		break;
	case ROOM_MIDDLE:
		printf("in the big room\n");
		break;
	case ROOM_MIDDLE_ON_BUTTON:
		printf("in the big room, on the button\n");
		break;
	case ROOM_PENALTY_BOX:
		printf("in the penalty box\n");
		break;
	case ROOM_RIGHT:
		printf("right up, before the lasers\n");
		break;
	case ROOM_RIGHT_BETWEEN_LASERS_AND_FORCE_FIELD:
		printf("right up, between the lasers and force field\n");
		break;
	case ROOM_RIGHT_BEHIND_FORCE_FIELD:
		printf("right up, behind the force field\n");
		break;
	case ROOM_EXIT:
		printf("at the exit\n");
		break;
	default:
		printf("UNKNOWN\n");
		break;
	}
}

static void print_spot(enum spot r)
{
	switch (r) {
	case SPOT_UNSET:
		printf("-\n");
		break;
	case SPOT_LOBBY:
		printf("lobby\n");
		break;
	case SPOT_LEFT_BACK:
		printf("left back\n");
		break;
	case SPOT_MIDDLE_MIDDLE:
		printf("middle middle\n");
		break;
	case SPOT_MIDDLE_LEFT_BACK:
		printf("middle left back\n");
		break;
	case SPOT_PENALTY_BOX_GROUND:
		printf("penalty box, ground\n");
		break;
	case SPOT_PENALTY_BOX_LEFT_WALL:
		printf("penalty box, left wall\n");
		break;
	case SPOT_PENALTY_BOX_LONG_WALL:
		printf("penalty box, long wall\n");
		break;
	default:
		printf("UNKNOWN\n");
		break;
	}
}

static void print_state(struct state const *st)
{
	printf("distance\t\t: %d\n", st->distance);
	printf("how we got here:\t: %s\n", st->how);
	printf("code\t\t\t: a=%d p=%d || cc=%d lcl=%d lcr=%d || "
	       "ap=%d as=%d pp=%d ps=%d\n",
		st->atlas.room, st->p_body.room,
		st->companion_cube,
		st->lens_cube_left_back,
		st->lens_cube_right,
		st->atlas.primary, st->atlas.secondary,
		st->p_body.primary, st->p_body.secondary);
	printf("atlas is\t\t: "); print_room(st->atlas.room);
	printf("p-body is\t\t: "); print_room(st->p_body.room);
	printf("atlas primary is\t: "); print_spot(st->atlas.primary);
	printf("atlas secondary is\t: "); print_spot(st->atlas.secondary);
	printf("p-body primary is\t: "); print_spot(st->p_body.primary);
	printf("p-body secondary is\t: "); print_spot(st->p_body.secondary);
	printf("companion cube is\t: "); print_room(st->companion_cube);
	printf("lens cube L is\t\t: "); print_room(st->lens_cube_left_back);
	printf("lens cube R is\t\t: "); print_room(st->lens_cube_right);
	printf("\n");
}

static int is_of_interest_actors(struct state const const *st,
				 struct actor_state const *a,
				 struct actor_state const *b)
{
	int yay;

	yay = 0;

	if (st->lens_cube_left_back == ROOM_LEFT_FRONT ||
	    st->lens_cube_right == ROOM_LEFT_FRONT)
		yay++;

	if (st->lens_cube_left_back == ROOM_LOBBY ||
	    st->lens_cube_right == ROOM_LOBBY)
		yay++;

	if (st->companion_cube == ROOM_PENALTY_BOX)
		yay++;

	if (a->room == ROOM_RIGHT_BEHIND_FORCE_FIELD &&
	    ((a->primary == SPOT_LEFT_BACK && a->secondary == SPOT_LOBBY) ||
	    (a->primary == SPOT_LOBBY && a->secondary == SPOT_LEFT_BACK)))
		yay++;

	if (b->room == ROOM_MIDDLE &&
	    ((b->primary == SPOT_MIDDLE_LEFT_BACK && b->secondary == SPOT_PENALTY_BOX_GROUND) ||
	    (b->primary == SPOT_PENALTY_BOX_GROUND && b->secondary == SPOT_MIDDLE_LEFT_BACK)))
		yay++;

	return (yay >= 5) ? 1 : 0;
}

static int is_of_interest(struct state const *st)
{
	static int seen;

#if 0
	if (!seen && left_funnel_active(st)) {
		seen = 1;
		return 1;
	}
#endif

#if 0
	if (!seen && st->lens_cube_left_back == ROOM_MIDDLE_ON_BUTTON) {
		seen = 1;
		return 1;
	}
#endif

#if 0
	if (!seen && cubes_in_room(st, ROOM_LEFT_FRONT) >= 3) {
		seen = 1;
		return 1;
	}
#endif

#if 0
	if (!seen && funnel_to_exit(st) &&
	    cubes_in_room(st, ROOM_RIGHT_BEHIND_FORCE_FIELD)) {
		seen = 1;
		return 1;
	}
#endif

#if 0
	if (!seen && st->atlas.room == ROOM_RIGHT_BEHIND_FORCE_FIELD &&
	    st->p_body.room == ROOM_RIGHT_BEHIND_FORCE_FIELD) {
		seen = 1;
		return 1;
	}
#endif

#if 0
	if (!seen && funnel_to_exit(st) &&
	    cubes_in_room(st, ROOM_RIGHT_BEHIND_FORCE_FIELD) &&
	    (st->atlas.room == ROOM_RIGHT_BEHIND_FORCE_FIELD ||
	     st->p_body.room == ROOM_RIGHT_BEHIND_FORCE_FIELD)) {
		seen = 1;
		return 1;
	}
#endif

#if 0
	if (!seen && (is_of_interest_actors(st, &st->atlas, &st->p_body) ||
	    is_of_interest_actors(st, &st->p_body, &st->atlas))) {
		seen = 1;
		return 1;
	}
#endif

	if (st->atlas.room == ROOM_EXIT &&
	    st->p_body.room == ROOM_EXIT &&
	    cubes_in_room(st, ROOM_EXIT))
		return 1;

	return 0;
}

static void backtrace_my_ip(struct state const *st)
{
	if (st->prev != NULL)
		backtrace_my_ip(st->prev);
	print_state(st);
}

static void
add_reachable_state(struct state const *_st,
		    struct state const *prev, char const *how)
{
	struct state *st;

	if (have_equiv_state(_st))
		return;

	st = malloc(sizeof(*st));
	if (st == NULL) {
		fprintf(stderr, "out of memory\n");
		abort();
	}

	memcpy(st, _st, sizeof(*st));
	st->num = num_reachable_states++;
	st->distance = (prev != NULL) ? prev->distance + 1 : 0;
	st->prev = prev;
	st->how = how;
	iv_avl_tree_insert(&reachable_states, &st->an);
	iv_list_add_tail(&st->process, &to_be_processed);

#if 1
	if (1) {
		static int last_dist = -1;
		static int num_of_dat;

		if (last_dist != st->distance) {
			printf("dist %d: %d states\n", last_dist, num_of_dat);
			last_dist = st->distance;
			num_of_dat = 0;
		}
		num_of_dat++;
	}

	if ((st->num % 10000) == 0) {
		printf("adding state %d (dist %d)\n", st->num, st->distance);
//		print_state(st);
		fflush(stdout);
	}
#else
	printf("adding state %d from %d (dist %d)\n",
	       st->num, (prev != NULL) ? prev->num : -1, st->distance);
	print_state(st);
#endif

	if (is_of_interest(st)) {
		printf("FOUND INTERESTING STATE!!!!!\n");
		printf("============================\n");
		backtrace_my_ip(st);
		printf("have left force field: %d\n", have_left_force_field(st));
		printf("have light bridge: %d\n", have_light_bridge(st));
		printf("have right force field: %d\n", have_right_force_field(st));
		printf("have laser out penalty box long wall: %d\n", have_laser_out_penalty_box_long_wall(st));
		printf("left funnel active: %d\n", left_funnel_active(st));
		printf("have laser out left back: %d\n", have_laser_out_left_back(st));
		printf("have laser cube in left front: %d\n", have_laser_cube_in_left_front(st));
		printf("middle funnel active: %d\n", middle_funnel_active(st));
		printf("funnel to pillar: %d\n", funnel_to_pillar(st));
		printf("funnel to exit: %d\n", funnel_to_exit(st));
		printf("======================\n");
		printf("\n");
		exit(0);
	}
}

static void init(void)
{
	INIT_IV_AVL_TREE(&reachable_states, compare_states_avl);
	num_reachable_states = 0;
	INIT_IV_LIST_HEAD(&to_be_processed);

	initial_state.atlas.room = ROOM_LOBBY;
	initial_state.atlas.primary = SPOT_UNSET;
	initial_state.atlas.secondary = SPOT_UNSET;
	initial_state.p_body.room = ROOM_LOBBY;
	initial_state.p_body.primary = SPOT_UNSET;
	initial_state.p_body.secondary = SPOT_UNSET;
	initial_state.companion_cube = ROOM_MIDDLE;
	initial_state.lens_cube_left_back = ROOM_LEFT_BACK;
	initial_state.lens_cube_right = ROOM_RIGHT_BEHIND_FORCE_FIELD;
	add_reachable_state(&initial_state, NULL, "initial");
}


/* spot mapping *************************************************************/
static enum room spot_to_room(enum spot sp)
{
	switch (sp) {
	case SPOT_UNSET:
		return -1;
	case SPOT_LOBBY:
		return ROOM_LOBBY;
	case SPOT_LEFT_BACK:
		return ROOM_LEFT_BACK;
	case SPOT_MIDDLE_MIDDLE:
		return ROOM_MIDDLE;
	case SPOT_MIDDLE_LEFT_BACK:
		return -1;
	case SPOT_PENALTY_BOX_GROUND:
	case SPOT_PENALTY_BOX_LEFT_WALL:
	case SPOT_PENALTY_BOX_LONG_WALL:
		return ROOM_PENALTY_BOX;
	default:
		fprintf(stderr, "spot_to_room(%d): fail\n", sp);
		abort();
	}
}


/* actor travel *************************************************************/
static void travel(struct state const *st, struct state *tmp,
		   struct actor_state const *a, struct actor_state *tmpa,
		   enum room room_to)
{
	/* travel alone */
	*tmp = *st;
	tmpa->room = room_to;
	add_reachable_state(tmp, st, "travel");

	/* travel with a cube */
	if (a->room == st->companion_cube) {
		*tmp = *st;
		tmpa->room = room_to;
		tmp->companion_cube = room_to;
		add_reachable_state(tmp, st, "travel with cube");
	}

	if (a->room == st->lens_cube_left_back) {
		*tmp = *st;
		tmpa->room = room_to;
		tmp->lens_cube_left_back = room_to;
		add_reachable_state(tmp, st, "travel with cube");
	}

	if (a->room == st->lens_cube_right) {
		*tmp = *st;
		tmpa->room = room_to;
		tmp->lens_cube_right = room_to;
		add_reachable_state(tmp, st, "travel with cube");
	}
}

static void travel_through_force_field(struct state const *st,
				       struct state *tmp,
				       struct actor_state const *a,
				       struct actor_state *tmpa,
				       enum room room_to)
{
	/* travel alone */
	*tmp = *st;
	tmpa->room = room_to;
	tmpa->primary = SPOT_UNSET;
	tmpa->secondary = SPOT_UNSET;
	add_reachable_state(tmp, st, "travel through force field");

	/* travel with a cube -- this will destroy it */
	if (a->room == st->companion_cube) {
		*tmp = *st;
		tmpa->room = room_to;
		tmpa->primary = SPOT_UNSET;
		tmpa->secondary = SPOT_UNSET;
		tmp->companion_cube = initial_state.companion_cube;
		add_reachable_state(tmp, st, "travel through force field with cube");
	}

	if (a->room == st->lens_cube_left_back) {
		*tmp = *st;
		tmpa->room = room_to;
		tmpa->primary = SPOT_UNSET;
		tmpa->secondary = SPOT_UNSET;
		tmp->lens_cube_left_back = initial_state.lens_cube_left_back;
		add_reachable_state(tmp, st, "travel through force field with cube");
	}

	if (a->room == st->lens_cube_right) {
		*tmp = *st;
		tmpa->room = room_to;
		tmpa->primary = SPOT_UNSET;
		tmpa->secondary = SPOT_UNSET;
		tmp->lens_cube_right = initial_state.lens_cube_right;
		add_reachable_state(tmp, st, "travel through force field with cube");
	}
}

/* portal setting ***********************************************************/
static void portal_set(struct state const *st, struct state *tmp,
		       struct actor_state const *a, struct actor_state *tmpa,
		       enum spot spot_to)
{
	enum spot prim;

	*tmp = *st;

	if (spot_to == SPOT_LEFT_BACK ||
	    spot_to == SPOT_MIDDLE_MIDDLE ||
	    spot_to == SPOT_MIDDLE_LEFT_BACK ||
	    spot_to == SPOT_PENALTY_BOX_GROUND ||
	    spot_to == SPOT_PENALTY_BOX_LEFT_WALL) {
		if (tmp->atlas.primary == spot_to)
			tmp->atlas.primary = SPOT_UNSET;
		if (tmp->atlas.secondary == spot_to)
			tmp->atlas.secondary = SPOT_UNSET;
		if (tmp->p_body.primary == spot_to)
			tmp->p_body.primary = SPOT_UNSET;
		if (tmp->p_body.secondary == spot_to)
			tmp->p_body.secondary = SPOT_UNSET;
	}

	prim = tmpa->primary;
	tmpa->primary = spot_to;
	add_reachable_state(tmp, st, "set portal");

	tmpa->primary = prim;
	tmpa->secondary = spot_to;
	add_reachable_state(tmp, st, "set portal");
}

/* portal travel ************************************************************/
static void portal_travel_to(struct state const *st, struct state *tmp,
			     struct actor_state const *a,
			     struct actor_state *tmpa, enum spot spot_to)
{
	enum room room_to;

	room_to = spot_to_room(spot_to);
	if (room_to == -1)
		return;

	/* travel alone */
	*tmp = *st;
	tmpa->room = room_to;
	add_reachable_state(tmp, st, "travel through portal");

	/* travel with a cube */
	if (a->room == st->companion_cube) {
		*tmp = *st;
		tmpa->room = room_to;
		tmp->companion_cube = room_to;
		add_reachable_state(tmp, st, "travel through portal with cube");
	}

	/* travel with a cube */
	if (a->room == st->lens_cube_left_back) {
		*tmp = *st;
		tmpa->room = room_to;
		tmp->lens_cube_left_back = room_to;
		add_reachable_state(tmp, st, "travel through portal with cube");
	}

	/* travel with a cube */
	if (a->room == st->lens_cube_right) {
		*tmp = *st;
		tmpa->room = room_to;
		tmp->lens_cube_right = room_to;
		add_reachable_state(tmp, st, "travel through portal with cube");
	}
}

static void portal_travel(struct state const *st, struct state *tmp,
			  struct actor_state const *a,
			  struct actor_state *tmpa, enum spot from)
{
	if (st->atlas.primary == from)
		portal_travel_to(st, tmp, a, tmpa, st->atlas.secondary);
	if (st->atlas.secondary == from)
		portal_travel_to(st, tmp, a, tmpa, st->atlas.primary);
	if (st->p_body.primary == from)
		portal_travel_to(st, tmp, a, tmpa, st->p_body.secondary);
	if (st->p_body.secondary == from)
		portal_travel_to(st, tmp, a, tmpa, st->p_body.primary);
}


/* cube push ****************************************************************/
static void cube_push_from_to(struct state const *st, struct state *tmp,
			      enum spot spot_from, enum spot spot_to)
{
	enum room room_from;
	enum room room_to;

        room_from = spot_to_room(spot_from);
        if (room_from == -1)
		abort();

        room_to = spot_to_room(spot_to);
        if (room_to == -1)
                return;

	if (st->companion_cube == room_from) {
		*tmp = *st;
		tmp->companion_cube = room_to;
		add_reachable_state(tmp, st, "cube push");
	}

	if (st->lens_cube_left_back == room_from) {
		*tmp = *st;
		tmp->lens_cube_left_back = room_to;
		add_reachable_state(tmp, st, "cube push");
	}

	if (st->lens_cube_right == room_from) {
		*tmp = *st;
		tmp->lens_cube_right = room_to;
		add_reachable_state(tmp, st, "cube push");
	}
}

static void cube_push(struct state const *st, struct state *tmp,
		      enum spot from)
{
	if (st->atlas.primary == from)
		cube_push_from_to(st, tmp, from, st->atlas.secondary);
	if (st->atlas.secondary == from)
		cube_push_from_to(st, tmp, from, st->atlas.primary);
	if (st->p_body.primary == from)
		cube_push_from_to(st, tmp, from, st->p_body.secondary);
	if (st->p_body.secondary == from)
		cube_push_from_to(st, tmp, from, st->p_body.primary);
}


/* cube pull ****************************************************************/
static void cube_pull_from_to(struct state const *st, struct state *tmp,
			      enum spot spot_from, enum spot spot_to, enum room room_to)
{
	enum room room_from;

        room_from = spot_to_room(spot_from);
        if (room_from == -1)
                return;

}

static void cube_pull(struct state const *st, struct state *tmp,
		      enum room room_from, enum room room_to)
{
	if (st->companion_cube == room_from) {
		*tmp = *st;
		tmp->companion_cube = room_to;
		add_reachable_state(tmp, st, "cube pull");
	}

	if (st->lens_cube_left_back == room_from) {
		*tmp = *st;
		tmp->lens_cube_left_back = room_to;
		add_reachable_state(tmp, st, "cube pull");
	}

	if (st->lens_cube_right == room_from) {
		*tmp = *st;
		tmp->lens_cube_right = room_to;
		add_reachable_state(tmp, st, "cube pull");
	}
}


/* cube destruction *********************************************************/
static void cube_destroy(struct state const *st, struct state *tmp,
			 enum room room)
{
	if (st->companion_cube == room) {
		*tmp = *st;
		tmp->companion_cube = initial_state.companion_cube;
		add_reachable_state(tmp, st, "cube destroy");
	}

	if (st->lens_cube_left_back == room) {
		*tmp = *st;
		tmp->lens_cube_left_back = initial_state.lens_cube_left_back;
		add_reachable_state(tmp, st, "cube destroy");
	}

	if (st->lens_cube_right == room) {
		*tmp = *st;
		tmp->lens_cube_right = initial_state.lens_cube_right;
		add_reachable_state(tmp, st, "cube destroy");
	}
}


/* suicide ******************************************************************/
static void suicide(struct state const *st, struct state *tmp,
		    struct actor_state const *a, struct actor_state *tmpa)
{
	*tmp = *st;
	tmpa->room = ROOM_LOBBY;
	tmpa->primary = SPOT_UNSET;
	tmpa->secondary = SPOT_UNSET;
	add_reachable_state(tmp, st, "suicide");
}


/* state transitions ********************************************************/
static void in_lobby(struct state const *st, struct state *tmp,
		     struct actor_state const *a, struct actor_state *tmpa)
{
	/* travel */
	travel_through_force_field(st, tmp, a, tmpa, ROOM_MIDDLE);

	/* portal set */
	portal_set(st, tmp, a, tmpa, SPOT_LOBBY);

	/* portal travel */
	portal_travel(st, tmp, a, tmpa, SPOT_LOBBY);
}

static void in_left_front(struct state const *st, struct state *tmp,
			  struct actor_state const *a, struct actor_state *tmpa)
{
	/* travel */
	travel(st, tmp, a, tmpa, ROOM_LOBBY);
	if (have_left_force_field(st))
		travel_through_force_field(st, tmp, a, tmpa, ROOM_LEFT_BACK);
	else
		travel(st, tmp, a, tmpa, ROOM_LEFT_BACK);
	if (have_light_bridge(st))
		travel(st, tmp, a, tmpa, ROOM_RIGHT);

	/* portal set */
	portal_set(st, tmp, a, tmpa, SPOT_LOBBY);
	if (!have_left_force_field(st))
		portal_set(st, tmp, a, tmpa, SPOT_LEFT_BACK);

	/* cube destruction */
	if (have_left_force_field(st))
		cube_destroy(st, tmp, ROOM_LEFT_FRONT);
}

static void in_left_back(struct state const *st, struct state *tmp,
			 struct actor_state const *a, struct actor_state *tmpa)
{
	/* travel */
	if (have_left_force_field(st))
		travel_through_force_field(st, tmp, a, tmpa, ROOM_LEFT_FRONT);
	else
		travel(st, tmp, a, tmpa, ROOM_LEFT_FRONT);

	/* portal set */
	portal_set(st, tmp, a, tmpa, SPOT_LEFT_BACK);

	/* portal travel */
	portal_travel(st, tmp, a, tmpa, SPOT_LEFT_BACK);

	/* cube destruction */
	if (have_left_force_field(st))
		cube_destroy(st, tmp, ROOM_LEFT_BACK);
}

static void in_middle(struct state const *st, struct state *tmp,
		      struct actor_state const *a, struct actor_state *tmpa)
{
	/* travel */
	travel_through_force_field(st, tmp, a, tmpa, ROOM_LOBBY);

	if (have_light_bridge(st))
		travel_through_force_field(st, tmp, a, tmpa, ROOM_LEFT_FRONT);

	if (cubes_in_room(st, ROOM_MIDDLE))
		travel(st, tmp, a, tmpa, ROOM_MIDDLE_ON_BUTTON);

	if (have_light_bridge(st))
		travel_through_force_field(st, tmp, a, tmpa, ROOM_RIGHT);

	/* portal set */
	portal_set(st, tmp, a, tmpa, SPOT_MIDDLE_MIDDLE);
	portal_set(st, tmp, a, tmpa, SPOT_MIDDLE_LEFT_BACK);
	portal_set(st, tmp, a, tmpa, SPOT_PENALTY_BOX_GROUND);
	portal_set(st, tmp, a, tmpa, SPOT_PENALTY_BOX_LEFT_WALL);

	/* cube destruction */
	cube_destroy(st, tmp, ROOM_MIDDLE);

	/* suicide */
	suicide(st, tmp, a, tmpa);

	/* cube push */
	cube_push(st, tmp, SPOT_MIDDLE_MIDDLE);
}

static void in_middle_on_button(struct state const *st, struct state *tmp,
				struct actor_state const *a,
				struct actor_state *tmpa)
{
	/* travel */
	travel(st, tmp, a, tmpa, ROOM_MIDDLE);

	/* portal set */
	portal_set(st, tmp, a, tmpa, SPOT_MIDDLE_MIDDLE);
	portal_set(st, tmp, a, tmpa, SPOT_MIDDLE_LEFT_BACK);
	portal_set(st, tmp, a, tmpa, SPOT_PENALTY_BOX_GROUND);
	portal_set(st, tmp, a, tmpa, SPOT_PENALTY_BOX_LEFT_WALL);
	portal_set(st, tmp, a, tmpa, SPOT_PENALTY_BOX_LONG_WALL); /* @@@ */

	/* cube destruction */
	cube_destroy(st, tmp, ROOM_MIDDLE_ON_BUTTON);

	/* suicide */
	suicide(st, tmp, a, tmpa);
}

static void in_penalty_box(struct state const *st, struct state *tmp,
			   struct actor_state const *a,
			   struct actor_state *tmpa)
{
	/* portal set */
	portal_set(st, tmp, a, tmpa, SPOT_MIDDLE_MIDDLE);
	portal_set(st, tmp, a, tmpa, SPOT_MIDDLE_LEFT_BACK); /* @@@ */
	portal_set(st, tmp, a, tmpa, SPOT_PENALTY_BOX_GROUND);
	portal_set(st, tmp, a, tmpa, SPOT_PENALTY_BOX_LEFT_WALL);
	portal_set(st, tmp, a, tmpa, SPOT_PENALTY_BOX_LONG_WALL);

	/* portal travel */
	portal_travel(st, tmp, a, tmpa, SPOT_PENALTY_BOX_GROUND);
	portal_travel(st, tmp, a, tmpa, SPOT_PENALTY_BOX_LEFT_WALL);
	portal_travel(st, tmp, a, tmpa, SPOT_PENALTY_BOX_LONG_WALL);

	/* cube destruction */
	cube_destroy(st, tmp, ROOM_PENALTY_BOX);

	/* suicide */
	suicide(st, tmp, a, tmpa);
}

static void in_right(struct state const *st, struct state *tmp,
		     struct actor_state const *a, struct actor_state *tmpa,
		     struct actor_state const *b, struct actor_state *tmpb)
{
	/* travel */
	travel(st, tmp, a, tmpa, ROOM_LOBBY);

	if (have_light_bridge(st))
		travel(st, tmp, a, tmpa, ROOM_LEFT_FRONT);

	if (b->room == ROOM_RIGHT)
		travel(st, tmp, a, tmpa, ROOM_RIGHT_BETWEEN_LASERS_AND_FORCE_FIELD);

	/* portal set */
	if (!have_right_force_field(st))
		portal_set(st, tmp, a, tmpa, SPOT_PENALTY_BOX_LEFT_WALL);

	/* suicide */
	suicide(st, tmp, a, tmpa);
}

static void
in_right_between_lasers_and_force_field(struct state const *st,
					struct state *tmp,
					struct actor_state const *a,
					struct actor_state *tmpa,
					struct actor_state const *b,
					struct actor_state *tmpb)
{
	/* travel */
	travel(st, tmp, a, tmpa, ROOM_RIGHT);

	if (b->room == ROOM_RIGHT)
		travel(st, tmp, b, tmpb, ROOM_RIGHT_BETWEEN_LASERS_AND_FORCE_FIELD);

	if (have_right_force_field(st))
		travel_through_force_field(st, tmp, a, tmpa, ROOM_RIGHT_BEHIND_FORCE_FIELD);
	else
		travel(st, tmp, a, tmpa, ROOM_RIGHT_BEHIND_FORCE_FIELD);

	/* cube destruction */
	if (have_right_force_field(st))
		cube_destroy(st, tmp, ROOM_RIGHT_BETWEEN_LASERS_AND_FORCE_FIELD);

	/* push cube through lasers */
	if (st->companion_cube == ROOM_RIGHT_BETWEEN_LASERS_AND_FORCE_FIELD) {
		*tmp = *st;
		tmp->companion_cube = ROOM_RIGHT;
		add_reachable_state(tmp, st, "push cube through lasers");
	}

	if (st->lens_cube_left_back == ROOM_RIGHT_BETWEEN_LASERS_AND_FORCE_FIELD) {
		*tmp = *st;
		tmp->lens_cube_left_back = ROOM_RIGHT;
		add_reachable_state(tmp, st, "push cube through lasers");
	}

	if (st->lens_cube_right == ROOM_RIGHT_BETWEEN_LASERS_AND_FORCE_FIELD) {
		*tmp = *st;
		tmp->lens_cube_right = ROOM_RIGHT;
		add_reachable_state(tmp, st, "push cube through lasers");
	}

	/* suicide */
	suicide(st, tmp, a, tmpa);
}

static void in_right_behind_force_field(struct state const *st,
					struct state *tmp,
					struct actor_state const *a,
					struct actor_state *tmpa)
{
	/* travel */
	if (have_right_force_field(st))
		travel_through_force_field(st, tmp, a, tmpa, ROOM_RIGHT_BETWEEN_LASERS_AND_FORCE_FIELD);
	else
		travel(st, tmp, a, tmpa, ROOM_RIGHT_BETWEEN_LASERS_AND_FORCE_FIELD);

	if (funnel_to_exit(st))
		travel(st, tmp, a, tmpa, ROOM_EXIT);

	/* portal set */
	portal_set(st, tmp, a, tmpa, SPOT_PENALTY_BOX_GROUND);
	portal_set(st, tmp, a, tmpa, SPOT_PENALTY_BOX_LEFT_WALL);
	portal_set(st, tmp, a, tmpa, SPOT_PENALTY_BOX_LONG_WALL);

	/* cube destruction */
	if (have_right_force_field(st))
		cube_destroy(st, tmp, ROOM_RIGHT_BEHIND_FORCE_FIELD);

	/* suicide */
	suicide(st, tmp, a, tmpa);

	/* cube push */
	cube_push(st, tmp, SPOT_PENALTY_BOX_GROUND);

	/* cube pull */
	cube_pull(st, tmp, ROOM_PENALTY_BOX, ROOM_RIGHT_BEHIND_FORCE_FIELD);
}

static void in_exit(struct state const *st, struct state *tmp,
		    struct actor_state const *a, struct actor_state *tmpa)
{
	/* travel */
	travel(st, tmp, a, tmpa, ROOM_RIGHT_BEHIND_FORCE_FIELD);

	/* portal set */
	portal_set(st, tmp, a, tmpa, SPOT_PENALTY_BOX_GROUND);
	portal_set(st, tmp, a, tmpa, SPOT_PENALTY_BOX_LEFT_WALL);
	portal_set(st, tmp, a, tmpa, SPOT_PENALTY_BOX_LONG_WALL);
}


/* state iteration **********************************************************/
static void process_for_actor(struct state const *st,
			      struct state *tmp,
			      struct actor_state const *a,
			      struct actor_state *tmpa,
			      struct actor_state const *b,
			      struct actor_state *tmpb)
{
	switch (a->room) {
        case ROOM_LOBBY:
		in_lobby(st, tmp, a, tmpa);
		break;
        case ROOM_LEFT_FRONT:
		in_left_front(st, tmp, a, tmpa);
		break;
        case ROOM_LEFT_BACK:
		in_left_back(st, tmp, a, tmpa);
		break;
        case ROOM_MIDDLE:
		in_middle(st, tmp, a, tmpa);
		break;
        case ROOM_MIDDLE_ON_BUTTON:
		in_middle_on_button(st, tmp, a, tmpa);
		break;
        case ROOM_PENALTY_BOX:
		in_penalty_box(st, tmp, a, tmpa);
		break;
        case ROOM_RIGHT:
		in_right(st, tmp, a, tmpa, b, tmpb);
		break;
        case ROOM_RIGHT_BETWEEN_LASERS_AND_FORCE_FIELD:
		in_right_between_lasers_and_force_field(st, tmp, a, tmpa, b, tmpb);
		break;
        case ROOM_RIGHT_BEHIND_FORCE_FIELD:
		in_right_behind_force_field(st, tmp, a, tmpa);
		break;
        case ROOM_EXIT:
		in_exit(st, tmp, a, tmpa);
		break;
	default:
		fprintf(stderr, "actor in room %d: fail\n", a->room);
		abort();
	}
}


static void process(void)
{
	while (!iv_list_empty(&to_be_processed)) {
		struct state *st;
		struct state tmp;

		st = iv_container_of(to_be_processed.next, struct state, process);
		iv_list_del(&st->process);

		process_for_actor(st, &tmp, &st->atlas,
				  &tmp.atlas, &st->p_body, &tmp.p_body);

		process_for_actor(st, &tmp, &st->p_body,
				  &tmp.p_body, &st->atlas, &tmp.atlas);

		if (left_funnel_active(st))
			cube_destroy(st, &tmp, ROOM_LEFT_FRONT);

		if (funnel_to_pillar(st)) {
			if (st->companion_cube == ROOM_MIDDLE) {
				tmp = *st;
				tmp.companion_cube = ROOM_MIDDLE_ON_BUTTON;
				add_reachable_state(&tmp, st, "move cube to button via funnel");
			}

			if (st->lens_cube_left_back == ROOM_MIDDLE) {
				tmp = *st;
				tmp.lens_cube_left_back = ROOM_MIDDLE_ON_BUTTON;
				add_reachable_state(&tmp, st, "move cube to button via funnel");
			}

			if (st->lens_cube_right == ROOM_MIDDLE) {
				tmp = *st;
				tmp.lens_cube_right = ROOM_MIDDLE_ON_BUTTON;
				add_reachable_state(&tmp, st, "move cube to button via funnel");
			}

			cube_destroy(st, &tmp, ROOM_MIDDLE);
			cube_destroy(st, &tmp, ROOM_MIDDLE_ON_BUTTON);
		}
	}
}


/* main *********************************************************************/
int main(void)
{
	init();
	process();

	fprintf(stderr, "%d states\n", num_reachable_states);

	return 0;
}
