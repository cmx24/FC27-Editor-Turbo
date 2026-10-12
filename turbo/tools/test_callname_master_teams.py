#!/usr/bin/env python3
"""Synthetic tests of build_callname_master with a team-aware bank (fc27_commentary "sets" / "teams" / "union", asset
catalog WP-04): the id lists are the team's sets, names cover LINK / HIGH players and ids without rows, the unnamed ones
are listed, a team other than the bank's primary set is refused. Synthetic names only. Needs openpyxl (no -I: it may be
a user-site package). Runs in about 2 s.

usage: python turbo/tools/test_callname_master_teams.py
"""
import json
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_callname_master as bcm  # noqa: E402

FAILS = []


def check(cond, what):
    print(('  ok    ' if cond else '  FAIL  ') + what)
    if not cond:
        FAILS.append(what)


def bank_with_sets():
    """eng_us-like: primary eng_us_dr_full (rows), dr_demo with an extra SIMPLE id, gm_full of the other team."""
    def ids(generic=(), simple=(), link=(), high=(), generic_high=()):
        return {'generic': list(generic), 'simple': list(simple), 'link': list(link), 'high': list(high),
                'generic_high': list(generic_high)}
    sets = {'eng_us_dr_full': {'team': 'dr', 'kind': 'full', 'ids': ids((900001,), (100, 200), (300,), (100,)),
                               'errors': {}},
            'eng_us_dr_demo': {'team': 'dr', 'kind': 'demo', 'ids': ids((), (100, 400), (), (600,)), 'errors': {}},
            'eng_us_gm_full': {'team': 'gm', 'kind': 'full', 'ids': ids((), (100, 700), (800,)),
                               'errors': {'pplayer_names_high': 'index without data is not an identity'}}}
    union = {'generic': [900001], 'simple': [100, 200, 400, 700], 'link': [300, 800], 'high': [100, 600],
             'generic_high': []}
    return {'turbo_bank': 1, 'lang': 'eng_us', 'game_build': '1.0.test', 'primary_set': 'eng_us_dr_full',
            'real': [{'segment': 1, 'variation': 1, 'playerid': 100}, {'segment': 2, 'variation': 1, 'playerid': 200}],
            'real_link': [{'segment': 3, 'variation': 1, 'playerid': 300}],
            'real_high': [{'segment': 4, 'variation': 1, 'playerid': 100}], 'generic_high': [],
            'generic': [{'segment': 5, 'variation': 1, 'commentaryid': 900001}],
            'sets': sets, 'teams': {}, 'union': union}


def main():
    t0 = time.time()
    b = bank_with_sets()
    print('team_ids:')
    t = bcm.team_ids(b)
    check(t['team'] == 'dr' and t['sets'] == ['eng_us_dr_demo', 'eng_us_dr_full'], 'default team = the primary set\'s')
    check(t['simple'] == [100, 200, 400] and t['link'] == [300] and t['high'] == [100, 600]
          and t['generic'] == [900001], 'the union of the team\'s sets')
    check(bcm.team_ids(b, kinds=['full'])['simple'] == [100, 200], '--kinds full: the full set only')
    try:
        bcm.team_ids(b, team='gm')
        check(False, 'a team other than the primary set\'s is refused')
    except ValueError as e:
        check('fc27_commentary --team gm' in str(e), 'a team other than the primary set\'s is refused')
    try:
        bcm.team_ids(b, kinds=['licensed'])
        check(False, 'no set of that kind: refused')
    except ValueError:
        check(True, 'no set of that kind: refused')
    check(bcm.team_ids({'real': []}) is None, 'an older bank (no sets): None, the rows decide as before')

    print('build:')
    with tempfile.TemporaryDirectory() as d:
        p = lambda n: os.path.join(d, n)  # noqa: E731
        template = p('test_master.xlsm')
        bcm._make_template(template)                       # names 100 'Old Name', 200 'Zeta' as 'real' rows
        with open(p('bank.json'), 'w', encoding='utf-8') as f:
            json.dump(b, f)
        with open(p('players.csv'), 'w', encoding='utf-8', newline='') as f:
            f.write('playerid,firstnameid,lastnameid,commonnameid,nationality\n100,1,2,0,27\n300,0,0,3,27\n')
        with open(p('edited.csv'), 'w', encoding='utf-8', newline='') as f:
            f.write('playerid,firstname,surname,commonname\n')
        with open(p('names.txt'), 'w', encoding='utf-8') as f:
            f.write('1\tTest\n2\tPlayer One\n3\tLink Only\n')
        with open(p('comm.txt'), 'w', encoding='utf-8') as f:
            f.write('900001\tSurname\n')
        with open(p('extra.json'), 'w', encoding='utf-8') as f:
            json.dump({'names': {'600': {'name': 'High Demo', 'source': 'test'}}}, f)
        out_json = p(os.path.join('out', 'eng_us.json'))
        logged = []
        out = bcm.build(p('bank.json'), p('players.csv'), p('edited.csv'), p('names.txt'), p('comm.txt'), template,
                        p(os.path.join('out', 'uk_master_fc27.xlsm')), out_json, log=logged.append,
                        extra_names=p('extra.json'))
        with open(out_json, encoding='utf-8') as f:
            j = json.load(f)
        check(j == out, 'the returned dict is the JSON written')
        check(j['real_players'] == [100, 200, 300, 400] and j['real_simple_players'] == [100, 200, 400]
              and j['real_link_players'] == [300] and j['real_high_players'] == [100, 600]
              and j['generic_ids'] == [900001], 'id lists = the dr sets (demo-only 400 / 600 included, gm 700 not)')
        check(j['team'] == 'dr' and j['bank_sets'] == ['eng_us_dr_demo', 'eng_us_dr_full']
              and j['real_players_all_teams'] == [100, 200, 300, 400, 700, 800], 'team, sets, all-teams list')
        check(j['names'] == {'100': 'Test Player One', '200': 'Zeta', '300': 'Link Only', '600': 'High Demo'},
              'names: SIMPLE (db, template), LINK (db), HIGH without rows (extra names)')
        check(j['unnamed_players'] == [400] and j['counts']['unnamed_players'] == 1
              and j['counts']['real_players_all_teams'] == 6, 'the unnamed player listed and counted')
        check(j['segments']['real'] == {'100': [1], '200': [2]}, 'segments stay the primary set\'s rows')
        check(any("team 'dr'" in x for x in logged) and any('without a name: 1' in x for x in logged),
              'team and unnamed players logged')
        try:
            bcm.build(p('bank.json'), p('players.csv'), p('edited.csv'), p('names.txt'), p('comm.txt'), template,
                      p(os.path.join('out2', 'x.xlsm')), p(os.path.join('out2', 'x.json')), log=lambda *_: None,
                      team='gm')
            check(False, '--team gm on a dr bank: refused before anything is written')
        except ValueError:
            check(not os.path.exists(p('out2')), '--team gm on a dr bank: refused before anything is written')

    print('test_callname_master_teams: %s (%.2f s)' % ('ok' if not FAILS else '%d failed' % len(FAILS),
                                                        time.time() - t0))
    return 0 if not FAILS else 1


if __name__ == '__main__':
    sys.exit(main())
