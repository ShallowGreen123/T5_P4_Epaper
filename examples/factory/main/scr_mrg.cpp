#include "scr_mrg.h"

static scr_card_t *s_registry_head = NULL;
static scr_card_t *s_registry_tail = NULL;
static scr_card_t *s_stack_root = NULL;
static scr_card_t *s_stack_top = NULL;

static uint32_t s_anim_time = SCR_MGR_ANIM_TIME;
static lv_scr_load_anim_t s_anim_switch = SCR_MGR_SCR_SWITCH_ANIM;
static lv_scr_load_anim_t s_anim_push = SCR_MGR_SCR_PUSH_ANIM;
static lv_scr_load_anim_t s_anim_pop = SCR_MGR_SCR_POP_ANIM;
static uint32_t s_default_bg_color = 0xFFFFFF;

static lv_obj_t *scr_mgr_create_obj(scr_card_t *card)
{
    lv_obj_t *obj = lv_obj_create(NULL);
    lv_obj_set_size(obj, lv_pct(100), lv_pct(100));
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(obj, lv_color_hex(s_default_bg_color), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    card->life->create(obj);
    return obj;
}

static scr_card_t *scr_mgr_find_by_id(int id)
{
    scr_card_t *card = s_registry_head ? s_registry_head->next : NULL;
    while (card != NULL) {
        if (card->id == id) {
            return card;
        }
        card = card->next;
    }
    return NULL;
}

static void scr_mgr_activate(scr_card_t *card)
{
    if (card == NULL || card->life == NULL) {
        return;
    }

    if (card->st == SCR_MGR_STATE_DESTROYED || card->st == SCR_MGR_STATE_IDLE) {
        card->obj = scr_mgr_create_obj(card);
        card->st = SCR_MGR_STATE_CREATED;
    }

    if (card->st == SCR_MGR_STATE_CREATED || card->st == SCR_MGR_STATE_INACTIVE) {
        if (card->life->entry) {
            card->life->entry();
        }
        card->st = SCR_MGR_STATE_ACTIVE;
    }
}

static void scr_mgr_inactivate(scr_card_t *card)
{
    if (card == NULL || card->life == NULL) {
        return;
    }

    if (card->st > SCR_MGR_STATE_INACTIVE) {
        if (card->life->exit) {
            card->life->exit();
        }
        card->st = SCR_MGR_STATE_INACTIVE;
    }
}

static void scr_mgr_remove(scr_card_t *card)
{
    if (card == NULL || card->life == NULL) {
        return;
    }

    if (card->st > SCR_MGR_STATE_INACTIVE && card->life->exit) {
        card->life->exit();
    }
    if (card->st > SCR_MGR_STATE_DESTROYED && card->life->destroy) {
        card->life->destroy();
    }
    card->st = SCR_MGR_STATE_DESTROYED;
}

void scr_mgr_init(void)
{
    if (s_registry_head != NULL) {
        return;
    }

    s_registry_head = (scr_card_t *)lv_mem_alloc(sizeof(scr_card_t));
    s_registry_head->id = -1;
    s_registry_head->obj = NULL;
    s_registry_head->st = (scr_mgr_state_e)-1;
    s_registry_head->life = NULL;
    s_registry_head->next = NULL;
    s_registry_head->prev = NULL;
    s_registry_tail = s_registry_head;

    s_stack_root = NULL;
    s_stack_top = NULL;
}

bool scr_mgr_register(int id, scr_lifecycle_t *card_life)
{
    if (s_registry_head == NULL || card_life == NULL || scr_mgr_find_by_id(id) != NULL) {
        return false;
    }

    scr_card_t *card = (scr_card_t *)lv_mem_alloc(sizeof(scr_card_t));
    card->id = id;
    card->obj = NULL;
    card->st = SCR_MGR_STATE_IDLE;
    card->life = card_life;
    card->next = NULL;
    card->prev = s_registry_tail;

    s_registry_tail->next = card;
    s_registry_tail = card;
    return true;
}

bool scr_mgr_switch(int id, bool anim)
{
    scr_card_t *target = scr_mgr_find_by_id(id);
    lv_obj_t *old_obj = NULL;

    if (target == NULL) {
        return false;
    }

    while (s_stack_top != NULL) {
        scr_card_t *prev = s_stack_top->prev;
        old_obj = s_stack_top->obj;
        scr_mgr_remove(s_stack_top);
        lv_mem_free(s_stack_top);
        s_stack_top = prev;
    }

    scr_card_t *stack_card = (scr_card_t *)lv_mem_alloc(sizeof(scr_card_t));
    stack_card->id = target->id;
    stack_card->obj = scr_mgr_create_obj(target);
    stack_card->st = SCR_MGR_STATE_CREATED;
    stack_card->life = target->life;
    stack_card->next = NULL;
    stack_card->prev = NULL;

    s_stack_root = stack_card;
    s_stack_top = stack_card;

    scr_mgr_activate(stack_card);

    if (anim && s_anim_switch != LV_SCR_LOAD_ANIM_NONE) {
        lv_scr_load_anim(stack_card->obj, s_anim_switch, s_anim_time, 0, true);
    } else {
        lv_scr_load(stack_card->obj);
        if (old_obj != NULL) {
            lv_obj_del(old_obj);
        }
    }

    return true;
}

bool scr_mgr_push(int id, bool anim)
{
    scr_card_t *target = scr_mgr_find_by_id(id);
    if (target == NULL) {
        return false;
    }

    if (s_stack_top != NULL && s_stack_top->id == target->id) {
        return false;
    }

    scr_card_t *stack_card = (scr_card_t *)lv_mem_alloc(sizeof(scr_card_t));
    stack_card->id = target->id;
    stack_card->obj = scr_mgr_create_obj(target);
    stack_card->st = SCR_MGR_STATE_CREATED;
    stack_card->life = target->life;
    stack_card->next = NULL;
    stack_card->prev = s_stack_top;

    if (s_stack_top != NULL) {
        scr_mgr_inactivate(s_stack_top);
        s_stack_top->next = stack_card;
    } else {
        s_stack_root = stack_card;
    }
    s_stack_top = stack_card;

    scr_mgr_activate(stack_card);

    if (anim && s_anim_push != LV_SCR_LOAD_ANIM_NONE) {
        lv_scr_load_anim(stack_card->obj, s_anim_push, s_anim_time, 0, false);
    } else {
        lv_scr_load(stack_card->obj);
    }

    return true;
}

bool scr_mgr_pop(bool anim)
{
    if (s_stack_top == NULL || s_stack_top == s_stack_root) {
        return false;
    }

    lv_obj_t *old_obj = s_stack_top->obj;
    scr_card_t *prev = s_stack_top->prev;
    scr_mgr_remove(s_stack_top);
    lv_mem_free(s_stack_top);
    s_stack_top = prev;
    if (s_stack_top != NULL) {
        s_stack_top->next = NULL;
        scr_mgr_activate(s_stack_top);
    }

    if (anim && s_anim_pop != LV_SCR_LOAD_ANIM_NONE) {
        lv_scr_load_anim(s_stack_top->obj, s_anim_pop, s_anim_time, 0, true);
    } else {
        lv_scr_load(s_stack_top->obj);
        if (old_obj != NULL) {
            lv_obj_del(old_obj);
        }
    }

    return true;
}

void scr_mgr_set_anim(lv_scr_load_anim_t sw, lv_scr_load_anim_t push, lv_scr_load_anim_t pop)
{
    if (sw >= LV_SCR_LOAD_ANIM_NONE && sw <= LV_SCR_LOAD_ANIM_OUT_BOTTOM) {
        s_anim_switch = sw;
    }
    if (push >= LV_SCR_LOAD_ANIM_NONE && push <= LV_SCR_LOAD_ANIM_OUT_BOTTOM) {
        s_anim_push = push;
    }
    if (pop >= LV_SCR_LOAD_ANIM_NONE && pop <= LV_SCR_LOAD_ANIM_OUT_BOTTOM) {
        s_anim_pop = pop;
    }
}

void scr_mgr_set_bg_color(uint32_t c)
{
    s_default_bg_color = c;
}

int scr_mgr_get_top_id(void)
{
    return s_stack_top ? s_stack_top->id : -1;
}

lv_obj_t *scr_mgr_get_top_obj(void)
{
    return s_stack_top ? s_stack_top->obj : NULL;
}
