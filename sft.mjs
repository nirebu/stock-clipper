import express from 'express';
import fs from 'node:fs';
console.log('express', (await import('express/package.json', {with:{type:'json'}})).default.version);
const app = express();
const paths = {
  '/css': '/home/nirebu/Work/stock/public/style.css',
  '/cache': '/home/nirebu/Work/stock/.cache/thumb-04b6afaeec7cc595bfe95f8b.jpg',
};
for (const [route, p] of Object.entries(paths)) {
  app.get(route, (req, res) => {
    try { console.log(route, 'stat:', fs.statSync(p).size); } catch (e) { console.log(route, 'STAT FAIL', e.code); }
    res.sendFile(p);
  });
}
app.use((err, _q, res, _n) => { console.log('ERR', err.status, err.message); res.status(500).send(err.message); });
app.listen(4399, '127.0.0.1', () => console.log('up', process.version));
