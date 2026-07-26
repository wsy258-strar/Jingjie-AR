CREATE TABLE IF NOT EXISTS scene_likes (
  scene_id VARCHAR(64) NOT NULL,
  user_id BIGINT UNSIGNED NOT NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (scene_id, user_id),
  INDEX idx_scene_likes_scene (scene_id)
);

CREATE TABLE IF NOT EXISTS scene_comments (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  scene_id VARCHAR(64) NOT NULL,
  user_id BIGINT UNSIGNED NOT NULL,
  content VARCHAR(300) NOT NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  INDEX idx_scene_comments_cursor (scene_id, id)
);
