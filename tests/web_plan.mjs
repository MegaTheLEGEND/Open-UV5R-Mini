// Node half of tests/web_plan_host.py: stdin {defaults, sets} -> stdout [plan for each set].
import { makePlan } from '../web/lib/plan.js';

let input = '';
process.stdin.on('data', d => { input += d; });
process.stdin.on('end', () => {
  const { defaults, sets } = JSON.parse(input);
  process.stdout.write(JSON.stringify(sets.map(s => makePlan(s, defaults, 'web'))));
});
